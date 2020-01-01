/*
 * Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "context.h"
#include "event.h"
#include "timer.h"
#include "smf-sm.h"

#include "pfcp-path.h"
#include "n4-handler.h"

void smf_pfcp_state_initial(ogs_fsm_t *s, smf_event_t *e)
{
    int rv;
    ogs_pfcp_node_t *pnode = NULL;

    ogs_assert(s);
    ogs_assert(e);

    smf_sm_debug(e);

    pnode = e->pnode;
    ogs_assert(pnode);

    rv = ogs_pfcp_connect(
            smf_self()->pfcp_sock, smf_self()->pfcp_sock6, pnode);
    ogs_assert(rv == OGS_OK);

    pnode->t_association = ogs_timer_add(smf_self()->timer_mgr,
            smf_timer_association, pnode);
    ogs_assert(pnode->t_association);
    pnode->t_heartbeat = ogs_timer_add(smf_self()->timer_mgr,
            smf_timer_heartbeat, pnode);
    ogs_assert(pnode->t_heartbeat);

    OGS_FSM_TRAN(s, &smf_pfcp_state_will_associate);
}

void smf_pfcp_state_final(ogs_fsm_t *s, smf_event_t *e)
{
    ogs_pfcp_node_t *pnode = NULL;
    ogs_assert(s);
    ogs_assert(e);

    smf_sm_debug(e);

    pnode = e->pnode;
    ogs_assert(pnode);

    ogs_timer_delete(pnode->t_association);
    ogs_timer_delete(pnode->t_heartbeat);
}

void smf_pfcp_state_will_associate(ogs_fsm_t *s, smf_event_t *e)
{
    char buf[OGS_ADDRSTRLEN];

    ogs_pfcp_node_t *pnode = NULL;
    ogs_pfcp_xact_t *xact = NULL;
    ogs_pfcp_message_t *message = NULL;

    ogs_sockaddr_t *addr = NULL;

    ogs_assert(s);
    ogs_assert(e);

    smf_sm_debug(e);

    pnode = e->pnode;
    ogs_assert(pnode);
    addr = pnode->sa_list;
    ogs_assert(addr);

    switch (e->id) {
    case OGS_FSM_ENTRY_SIG:
        ogs_timer_start(pnode->t_association,
                smf_timer_cfg(SMF_TIMER_ASSOCIATION)->duration);

        smf_pfcp_send_association_setup_request(pnode);
        break;
    case OGS_FSM_EXIT_SIG:
        ogs_timer_stop(pnode->t_association);
        break;
    case SMF_EVT_N4_TIMER:
        switch(e->timer_id) {
        case SMF_TIMER_ASSOCIATION:
            pnode = e->pnode;
            ogs_assert(pnode);

            ogs_warn("Connect to UPF [%s]:%d failed",
                        OGS_ADDR(addr, buf), OGS_PORT(addr));

            ogs_timer_start(pnode->t_association,
                smf_timer_cfg(SMF_TIMER_ASSOCIATION)->duration);

            smf_pfcp_send_association_setup_request(pnode);
            break;
        default:
            ogs_error("Unknown timer[%s:%d]",
                    smf_timer_get_name(e->timer_id), e->timer_id);
            break;
        }
        break;
    case SMF_EVT_N4_MESSAGE:
        message = e->pfcp_message;
        ogs_assert(message);
        xact = e->pfcp_xact;
        ogs_assert(xact);

        switch (message->h.type) {
        case OGS_PFCP_ASSOCIATION_SETUP_REQUEST_TYPE:
            smf_n4_handle_association_setup_request(pnode, xact,
                    &message->pfcp_association_setup_request);
            OGS_FSM_TRAN(s, smf_pfcp_state_associated);
            break;
        case OGS_PFCP_ASSOCIATION_SETUP_RESPONSE_TYPE:
            smf_n4_handle_association_setup_response(pnode, xact,
                    &message->pfcp_association_setup_response);
            OGS_FSM_TRAN(s, smf_pfcp_state_associated);
            break;
        default:
            ogs_error("cannot handle PFCP message type[%d]",
                    message->h.type);
            break;
        }
        break;
    default:
        ogs_error("Unknown event %s", smf_event_get_name(e));
        break;
    }
}

void smf_pfcp_state_associated(ogs_fsm_t *s, smf_event_t *e)
{
    char buf[OGS_ADDRSTRLEN];

    ogs_pfcp_node_t *pnode = NULL;
    ogs_pfcp_xact_t *xact = NULL;
    ogs_pfcp_message_t *message = NULL;

    ogs_sockaddr_t *addr = NULL;
    smf_sess_t *sess = NULL;

    ogs_assert(s);
    ogs_assert(e);

    smf_sm_debug(e);

    pnode = e->pnode;
    ogs_assert(pnode);
    addr = pnode->sa_list;
    ogs_assert(addr);

    switch (e->id) {
    case OGS_FSM_ENTRY_SIG:
        ogs_info("PFCP associated");
        ogs_timer_start(pnode->t_heartbeat,
                smf_timer_cfg(SMF_TIMER_HEARTBEAT)->duration);
        break;
    case OGS_FSM_EXIT_SIG:
        ogs_info("PFCP de-associated");
        ogs_timer_stop(pnode->t_heartbeat);
        break;
    case SMF_EVT_N4_MESSAGE:
        message = e->pfcp_message;
        ogs_assert(message);
        xact = e->pfcp_xact;
        ogs_assert(xact);

        if (message->h.seid_p)
            sess = smf_sess_find_by_seid(message->h.seid);

        switch (message->h.type) {
        case OGS_PFCP_HEARTBEAT_REQUEST_TYPE:
            smf_n4_handle_heartbeat_request(pnode, xact,
                    &message->pfcp_heartbeat_request);
            break;
        case OGS_PFCP_HEARTBEAT_RESPONSE_TYPE:
            smf_n4_handle_heartbeat_response(pnode, xact,
                    &message->pfcp_heartbeat_response);
            break;
        case OGS_PFCP_PFD_MANAGEMENT_REQUEST_TYPE:
            break;
        case OGS_PFCP_PFD_MANAGEMENT_RESPONSE_TYPE:
            break;
        case OGS_PFCP_ASSOCIATION_SETUP_REQUEST_TYPE:
            ogs_warn("PFCP[REQ] has already been associated");
            smf_n4_handle_association_setup_request(pnode, xact,
                    &message->pfcp_association_setup_request);
            break;
        case OGS_PFCP_ASSOCIATION_SETUP_RESPONSE_TYPE:
            ogs_warn("PFCP[RSP] has already been associated");
            smf_n4_handle_association_setup_response(pnode, xact,
                    &message->pfcp_association_setup_response);
            break;
        case OGS_PFCP_ASSOCIATION_UPDATE_REQUEST_TYPE:
            break;
        case OGS_PFCP_ASSOCIATION_UPDATE_RESPONSE_TYPE:
            break;
        case OGS_PFCP_ASSOCIATION_RELEASE_REQUEST_TYPE:
            break;
        case OGS_PFCP_ASSOCIATION_RELEASE_RESPONSE_TYPE:
            break;
        case OGS_PFCP_VERSION_NOT_SUPPORTED_RESPONSE_TYPE:
            break;
        case OGS_PFCP_NODE_REPORT_REQUEST_TYPE:
            break;
        case OGS_PFCP_NODE_REPORT_RESPONSE_TYPE:
            break;
        case OGS_PFCP_SESSION_SET_DELETION_REQUEST_TYPE:
            break;
        case OGS_PFCP_SESSION_SET_DELETION_RESPONSE_TYPE:
            break;
        case OGS_PFCP_SESSION_ESTABLISHMENT_REQUEST_TYPE:
            break;
        case OGS_PFCP_SESSION_ESTABLISHMENT_RESPONSE_TYPE:
            break;
        case OGS_PFCP_SESSION_MODIFICATION_REQUEST_TYPE:
            break;
        case OGS_PFCP_SESSION_MODIFICATION_RESPONSE_TYPE:
            break;
        case OGS_PFCP_SESSION_DELETION_REQUEST_TYPE:
            break;
        case OGS_PFCP_SESSION_DELETION_RESPONSE_TYPE:
            break;
        case OGS_PFCP_SESSION_REPORT_REQUEST_TYPE:
            break;
        case OGS_PFCP_SESSION_REPORT_RESPONSE_TYPE:
            break;
        default:
            ogs_error("Not implemented PFCP message type[%d]",
                    message->h.type);
            break;
        }

        break;
    case SMF_EVT_N4_TIMER:
        switch(e->timer_id) {
        case SMF_TIMER_HEARTBEAT:
            pnode = e->pnode;
            ogs_assert(pnode);

            smf_pfcp_send_heartbeat_request(pnode);
            break;
        default:
            ogs_error("Unknown timer[%s:%d]",
                    smf_timer_get_name(e->timer_id), e->timer_id);
            break;
        }
        break;
    case SMF_EVT_N4_NO_HEARTBEAT:
        ogs_warn("No heartbeat from UPF [%s]:%d",
                    OGS_ADDR(addr, buf), OGS_PORT(addr));
        OGS_FSM_TRAN(s, smf_pfcp_state_will_associate);
        break;
    default:
        ogs_error("Unknown event %s", smf_event_get_name(e));
        break;
    }
}

void smf_pfcp_state_exception(ogs_fsm_t *s, smf_event_t *e)
{
    ogs_assert(s);
    ogs_assert(e);

    smf_sm_debug(e);

    switch (e->id) {
    case OGS_FSM_ENTRY_SIG:
        break;
    case OGS_FSM_EXIT_SIG:
        break;
    default:
        ogs_error("Unknown event %s", smf_event_get_name(e));
        break;
    }
}
