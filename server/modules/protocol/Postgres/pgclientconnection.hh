/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "postgresprotocol.hh"
#include <maxscale/protocol2.hh>
#include <maxscale/session.hh>
#include <maxscale/queryclassifier.hh>

#include <vector>
#include <variant>

class PgProtocolData;
class PgUserCache;
class PgAuthenticatorModule;
class PgClientAuthenticator;

class PgClientConnection final : public mxs::ClientConnectionBase
{
public:
    struct UserAuthSettings
    {
        bool check_password {true};     /**< From listener */
        bool match_host_pattern {true}; /**< From listener */
        bool allow_root_user {false};   /**< From service */
    };

    PgClientConnection(MXS_SESSION* pSession,
                       mxs::Parser* pParser,
                       mxs::Component* pComponent,
                       const UserAuthSettings& auth_settings);

    // DCBHandler
    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    // mxs::ProtocolConnection
    bool write(GWBUF&& buffer) override;

    // mxs::ClientConnection
    bool init_connection() override;
    void finish_connection() override;
    bool clientReply(GWBUF&& buffer, mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    bool safe_to_restart() const override;
    mxs::Parser* parser() override;

    // mxs::ClientConnectionBase
    size_t sizeof_buffers() const override;

private:
    enum class State
    {
        INIT,          // Expecting either SSL request or Startup msg
        AUTH,          // Authentication (not entered if method is trust)
        ROUTE,         // Entered after Startup msg reply has been sent
        ERROR
    };

    using SimpleRequest = std::monostate;
    using HistoryRequest = std::unique_ptr<GWBUF>;

    State state_init(const GWBUF& gwbuf);
    State state_auth(GWBUF&& packet);
    State state_route(GWBUF&& gwbuf);

    // Return true if ssl handshake succeeded or is in progress
    bool setup_ssl();
    bool parse_startup_message(const GWBUF& buf);
    bool start_session();
    void update_user_account_entry();
    bool check_allow_login();
    void send_error(std::string_view sqlstate, std::string_view msg);
    bool record_for_history(GWBUF& buffer);
    void record_parse_for_history(GWBUF& buffer);

    void handle_response(SimpleRequest&& req, const mxs::Reply& reply);
    void handle_response(HistoryRequest&& req, const mxs::Reply& reply);

    const PgUserCache*     user_account_cache();
    PgAuthenticatorModule* find_auth_module(const std::string& auth_method);

    State           m_state = State::INIT;
    MXS_SESSION&    m_session;
    mxs::Parser&    m_parser;
    bool            m_ssl_required;
    mxs::Component* m_down;
    PgProtocolData* m_protocol_data {nullptr};

    // The "secret" key used when the connection is killed
    uint32_t m_secret {0};

    std::unique_ptr<PgClientAuthenticator> m_authenticator;
    const UserAuthSettings                 m_user_auth_settings;

    // The query classifier. Used to detect which statements need to be kept in the history.
    mariadb::QueryClassifier m_qc;

    // The ID generator for buffer IDs
    uint32_t m_next_id {1};

    // All the pending requests executed by the client
    std::vector<std::variant<SimpleRequest, HistoryRequest>> m_requests;
};
