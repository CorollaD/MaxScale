/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <maxscale/protocol2.hh>
#include "nosql.hh"
#include "nosqlconfig.hh"
#include "nosqlusermanager.hh"

class MYSQL_session;

class ClientConnection : public mxs::ClientConnection
{
public:
    ClientConnection(const Configuration& config,
                     nosql::UserManager* pUm,
                     MXS_SESSION* pSession,
                     mxs::Component* pComponent);
    ~ClientConnection();

    bool init_connection() override;

    void finish_connection() override;

    ClientDCB* dcb() override;
    const ClientDCB* dcb() const override;

    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool in_routing_state() const override
    {
        return true;
    }

    bool safe_to_restart() const override
    {
        return true;
    }

    using mxs::ClientConnection::parser;
    mxs::Parser* parser() override;

    void setup_session(const std::string& user, const std::vector<uint8_t>& password);
    bool start_session();

private:
    void ready_for_reading(GWBUF* pBuffer);

    // DCBHandler
    void ready_for_reading(DCB* dcb) override;
    void error(DCB* dcb, const char* errmsg) override;

private:
    // mxs::ProtocolConnection
    bool    write(GWBUF&& buffer) override;
    json_t* diagnostics() const override;
    void set_dcb(DCB* dcb) override;
    bool is_movable() const override;
    bool is_idle() const override;
    size_t sizeof_buffers() const override;

private:
    GWBUF* handle_one_packet(GWBUF* pPacket);

    void prepare_session(const std::string& user, const std::vector<uint8_t>& password);

    bool ssl_is_ready();
    bool setup_ssl();

private:
    nosql::Config  m_config;
    MXS_SESSION&   m_session;
    MYSQL_session& m_session_data;
    DCB*           m_pDcb = nullptr;
    nosql::NoSQL   m_nosql;
    bool           m_ssl_required;
};
