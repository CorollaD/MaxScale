/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "protocolmodule.hh"
#include <maxscale/protocol/mariadb/backend_connection.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "../MariaDB/user_data.hh"
#include "clientconnection.hh"
#include "nosqlcursor.hh"

using namespace std;

ProtocolModule::ProtocolModule(const std::string& name, std::unique_ptr<nosql::UserManager> sUm)
    : m_config(name, this)
    , m_sUm(std::move(sUm))
{
}

void ProtocolModule::post_configure()
{
    nosql::NoSQLCursor::start_purging_idle_cursors(m_config.cursor_timeout);
}

// static
ProtocolModule* ProtocolModule::create(const std::string& name)
{
    ProtocolModule* pThis = nullptr;

    unique_ptr<nosql::UserManager> sUm = nosql::UserManager::create(name);

    if (sUm)
    {
        pThis = new ProtocolModule(name, std::move(sUm));
    }

    return pThis;
}

unique_ptr<mxs::ClientConnection>
ProtocolModule::create_client_protocol(MXS_SESSION* pSession, mxs::Component* pComponent)
{
    unique_ptr<MYSQL_session> sSession_data(new MYSQL_session());
    // TODO: Drop this, operate on whatever data is delivered to clientReply() and
    // TODO: send documents to the client in multiple packets.
    sSession_data->set_client_protocol_capabilities(RCAP_TYPE_RESULTSET_OUTPUT);
    pSession->set_protocol_data(std::move(sSession_data));

    return unique_ptr<mxs::ClientConnection>(new ClientConnection(m_config,
                                                                  m_sUm.get(),
                                                                  pSession,
                                                                  pComponent));
}

unique_ptr<mxs::BackendConnection>
ProtocolModule::create_backend_protocol(MXS_SESSION* pSession,
                                        SERVER* pServer,
                                        mxs::Component* pComponent)
{
    return MariaDBBackendConnection::create(pSession, pComponent, *pServer);
}

string ProtocolModule::auth_default() const
{
    mxb_assert(!true);
    return "";
}

GWBUF* ProtocolModule::reject(const string& host)
{
    mxb_assert(!true);
    return nullptr;
}

uint64_t ProtocolModule::capabilities() const
{
    return mxs::ProtocolModule::CAP_BACKEND | mxs::ProtocolModule::CAP_AUTH_MODULES;
}

string ProtocolModule::name() const
{
    return MXS_MODULE_NAME;
}

unique_ptr<mxs::UserAccountManager> ProtocolModule::create_user_data_manager()
{
    return std::unique_ptr<mxs::UserAccountManager>(new MariaDBUserManager());
}

ProtocolModule::AuthenticatorList ProtocolModule::create_authenticators(const mxs::ConfigParameters& params)
{
    // TODO: For now we just load the default MariaDB authenticator.

    AuthenticatorList authenticators;

    string auth_name = MXS_MARIADBAUTH_AUTHENTICATOR_NAME;
    mxs::ConfigParameters auth_config;
    unique_ptr<mxs::AuthenticatorModule> sAuth_module = mxs::authenticator_init(auth_name, &auth_config);

    if (sAuth_module)
    {
        mxb_assert(strcasecmp(MXS_MARIADB_PROTOCOL_NAME,
                              sAuth_module->supported_protocol().c_str()) == 0);

        authenticators.push_back(move(sAuth_module));
    }
    else
    {
        MXB_ERROR("Failed to initialize authenticator module '%s'.", auth_name.c_str());
    }

    return authenticators;
}
