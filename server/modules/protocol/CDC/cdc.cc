/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file cdc.c - Change Data Capture Listener protocol module
 *
 * The change data capture protocol module is intended as a mechanism to allow connections
 * into maxscale for the purpose of accessing information within
 * the maxscale with a Change Data Capture API interface (supporting Avro right now)
 * databases.
 *
 * In the first instance it is intended to connect, authenticate and retieve data in the Avro format
 * as requested by compatible clients.
 *
 * @verbatim
 * Revision History
 * Date     Who         Description
 * 11/01/2016   Massimiliano Pinto  Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/protocol/cdc/module_names.hh>
#define MXS_MODULE_NAME MXS_CDC_PROTOCOL_NAME

#include <maxscale/ccdefs.hh>
#include <cstdio>
#include <cstring>
#include <maxbase/alloc.h>
#include <maxscale/protocol/cdc/cdc.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>
#include "cdc_plain_auth.hh"

class CDCProtocolModule : public mxs::ProtocolModule
{
public:
    ~CDCProtocolModule() override = default;

    static CDCProtocolModule* create()
    {
        return new(std::nothrow) CDCProtocolModule();
    }

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override
    {
        std::unique_ptr<mxs::ClientConnection> new_client_proto(
            new(std::nothrow) CDCClientConnection(m_auth_module));
        return new_client_proto;
    }

    std::string auth_default() const override
    {
        return "CDCPlainAuth";
    }

    std::string name() const override
    {
        return MXS_MODULE_NAME;
    }

    json_t* print_auth_users_json() override
    {
        return m_auth_module.diagnostics();
    }

private:
    CDCAuthenticatorModule m_auth_module;
};

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t args[] =
    {
        {MODULECMD_ARG_SERVICE, "Service where the user is added"},
        {MODULECMD_ARG_STRING,  "User to add"                    },
        {MODULECMD_ARG_STRING,  "Password of the user"           }
    };

    modulecmd_register_command("cdc", "add_user", MODULECMD_TYPE_ACTIVE, cdc_add_new_user,
                               3, args, "Add a new CDC user");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_PROTOCOL_VERSION,
        "A Change Data Capture Listener implementation for use in binlog events retrieval",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::ProtocolApiGenerator<CDCProtocolModule>::s_api,
        NULL,       /* Process init. */
        NULL,       /* Process finish. */
        NULL,       /* Thread init. */
        NULL,       /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

void CDCClientConnection::ready_for_reading(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);     // The protocol should only handle its own events.
    auto dcb = m_dcb;

    MXS_SESSION* session = dcb->session();
    CDCClientConnection* protocol = this;
    GWBUF* head = NULL;
    int auth_val = CDC_STATE_AUTH_FAILED;

    if (dcb->read(&head, 0) > 0)
    {
        switch (protocol->m_state)
        {
        case CDC_STATE_WAIT_FOR_AUTH:
            /* Fill CDC_session from incoming packet */
            if (m_authenticator.extract(dcb, head))
            {
                /* Call protocol authentication */
                auth_val = m_authenticator.authenticate(dcb);
            }

            /* Discard input buffer */
            gwbuf_free(head);

            if (auth_val == CDC_STATE_AUTH_OK)
            {
                if (session_start(dcb->session()))
                {
                    protocol->m_state = CDC_STATE_HANDLE_REQUEST;

                    write_auth_ack();
                }
                else
                {
                    auth_val = CDC_STATE_AUTH_NO_SESSION;
                }
            }

            if (auth_val != CDC_STATE_AUTH_OK)
            {
                protocol->m_state = CDC_STATE_AUTH_ERR;

                write_auth_err();
                /* force the client connection close */
                DCB::close(dcb);
            }
            break;

        case CDC_STATE_HANDLE_REQUEST:
            // handle CLOSE command, it shoudl be routed as well and client connection closed after last
            // transmission
            if (strncmp((char*)GWBUF_DATA(head), "CLOSE", GWBUF_LENGTH(head)) == 0)
            {
                MXS_INFO("%s: Client [%s] has requested CLOSE action",
                         dcb->service()->name(),
                         dcb->remote().c_str());

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                // the router will close the client connection
                // rc = mxs_route_query(session, head);

                // buffer not handled by router right now, consume it
                gwbuf_free(head);

                /* right now, just force the client connection close */
                DCB::close(dcb);
            }
            else
            {
                MXS_INFO("%s: Client [%s] requested [%.*s] action",
                         dcb->service()->name(),
                         dcb->remote().c_str(),
                         (int)GWBUF_LENGTH(head),
                         (char*)GWBUF_DATA(head));

                // gwbuf_set_type(head, GWBUF_TYPE_CDC);
                mxs_route_query(session, head);
            }
            break;

        default:
            MXS_INFO("%s: Client [%s] in unknown state %d",
                     dcb->service()->name(),
                     dcb->remote().c_str(),
                     protocol->m_state);
            gwbuf_free(head);

            break;
        }
    }
}

void CDCClientConnection::write_ready(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    m_dcb->writeq_drain();
}

int32_t CDCClientConnection::write(GWBUF* buffer)
{
    return m_dcb->writeq_append(buffer);
}

void CDCClientConnection::error(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    DCB::close(m_dcb);
}

void CDCClientConnection::hangup(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    DCB::close(m_dcb);
}

bool CDCClientConnection::init_connection()
{
    mxb_assert(m_dcb->session());

    /* client protocol state change to CDC_STATE_WAIT_FOR_AUTH */
    m_state = CDC_STATE_WAIT_FOR_AUTH;

    MXS_INFO("%s: new connection from [%s]", m_dcb->service()->name(), m_dcb->remote().c_str());
    return true;
}

void CDCClientConnection::finish_connection()
{
}

CDCClientConnection::CDCClientConnection(CDCAuthenticatorModule& auth_module)
    : m_authenticator(auth_module)
{
}

/**
 * Writes Authentication ACK, success.
 */
void CDCClientConnection::write_auth_ack()
{
    const char msg[] = "OK\n";
    auto buf = gwbuf_alloc_and_load(sizeof(msg) - 1, msg);
    write(buf);
}

/**
 * Writes Authentication ERROR.
 */
void CDCClientConnection::write_auth_err()
{
    const char msg[] = "ERROR: Authentication failed\n";
    auto buf = gwbuf_alloc_and_load(sizeof(msg) - 1, msg);
    write(buf);
}

bool CDCClientConnection::write(const char* msg)
{
    // CDC-protocol messages end in \n. The ending 0-char need not be written.
    auto len = strlen(msg);
    auto buf = gwbuf_alloc(len + 1);
    auto* ptr = GWBUF_DATA(buf);
    memcpy(ptr, msg, len);
    ptr += len;
    *ptr = '\n';
    return write(buf);
}
