/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <maxscale/protocol2.hh>
#include "configuration.hh"
#include "nosqlusermanager.hh"

class SERVICE;

class ProtocolModule : public mxs::ProtocolModule
{
public:
    using AuthenticatorList = std::vector<mxs::SAuthenticatorModule>;

    static ProtocolModule* create(const std::string& name, mxs::Listener* pListener);

    mxs::config::Configuration& getConfiguration() override final
    {
        return m_config;
    }

    std::unique_ptr<mxs::ClientConnection>
    create_client_protocol(MXS_SESSION* session, mxs::Component* component) override;

    std::unique_ptr<mxs::BackendConnection>
    create_backend_protocol(MXS_SESSION* session, SERVER* server, mxs::Component* component) override;

    std::string auth_default() const override;
    GWBUF*      reject(const std::string& host) override;

    uint64_t    capabilities() const override;
    std::string name() const override;

    std::unique_ptr<mxs::UserAccountManager> create_user_data_manager() override;

    AuthenticatorList create_authenticators(const mxs::ConfigParameters& params) override;

    bool post_configure();

private:
    ProtocolModule(std::string name, SERVICE* pService);

private:
    Configuration                       m_config;
    SERVICE&                            m_service;
    std::unique_ptr<nosql::UserManager> m_sUm;
};
