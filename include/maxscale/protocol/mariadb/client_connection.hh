/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/local_client.hh>

struct KillInfo;

/* Type of the kill-command sent by client. */
enum kill_type_t
{
    KT_CONNECTION = (1 << 0),
    KT_QUERY      = (1 << 1),
    KT_SOFT       = (1 << 2),
    KT_HARD       = (1 << 3)
};

class MariaDBUserManager;
class MariaDBUserCache;
struct UserEntryResult;

class MariaDBClientConnection : public mxs::ClientConnectionBase
{
public:
    MariaDBClientConnection(MXS_SESSION* session, mxs::Component* component);

    void ready_for_reading(DCB* dcb) override;
    void write_ready(DCB* dcb) override;
    void error(DCB* dcb) override;
    void hangup(DCB* dcb) override;

    int32_t write(GWBUF* buffer) override;

    bool    init_connection() override;
    void    finish_connection() override;
    int32_t connlimit(int limit) override;
    void    wakeup() override;
    bool    is_movable() const override;
    void    kill() override;

    std::string current_db() const override;

    static bool parse_kill_query(char* query, uint64_t* thread_id_out, kill_type_t* kt_out,
                                 std::string* user_out);

    /**
     * Kill a connection
     *
     * @param target_id The session ID in MaxScale to kill
     * @param type      The type of the KILL to perform
     * @param cb        Callback that is called once the KILL is complete
     *
     * @see kill_type_t
     */
    void mxs_mysql_execute_kill(uint64_t target_id, kill_type_t type, std::function<void()> cb);

    bool in_routing_state() const;

    json_t* diagnostics() const;

private:
    /** Return type of process_special_commands() */
    enum class SpecialCmdRes
    {
        CONTINUE,   // No special command detected, proceed as normal.
        END,        // Query handling completed, do not send to filters/router.
    };

    /** Return type of a lower level state machine */
    enum class StateMachineRes
    {
        IN_PROGRESS,// The SM should be called again once more data is available.
        DONE,       // The SM is complete for now, the protocol may advance to next state
        ERROR,      // The SM encountered an error. The connection should be closed.
    };

    enum class AuthType
    {
        NORMAL_AUTH,
        CHANGE_USER,
    };

    bool read_first_client_packet(mxs::Buffer* output);

    StateMachineRes process_handshake();
    StateMachineRes process_authentication(AuthType auth_type);
    StateMachineRes process_normal_read();

    int  send_mysql_client_handshake();
    bool parse_ssl_request_packet(GWBUF* buffer);
    bool parse_handshake_response_packet(GWBUF* buffer);

    bool perform_auth_exchange();
    void perform_check_token(AuthType auth_type);
    bool process_normal_packet(mxs::Buffer&& buffer);
    bool route_statement(mxs::Buffer&& buffer);

    bool start_change_user(mxs::Buffer&& buffer);
    bool complete_change_user();
    void cancel_change_user();

    void  handle_use_database(GWBUF* read_buffer);
    char* handle_variables(GWBUF** read_buffer);

    SpecialCmdRes process_special_commands(GWBUF* read_buffer, uint8_t cmd);
    SpecialCmdRes handle_query_kill(GWBUF* read_buffer, uint32_t packet_len);
    void          add_local_client(LocalClient* client);
    bool          have_local_clients();
    void          kill_complete(const std::function<void()>& cb, LocalClient* client);
    void          maybe_send_kill_response(const std::function<void()>& cb);

    void track_transaction_state(MXS_SESSION* session, GWBUF* packetbuf);
    void execute_kill_all_others(uint64_t target_id, uint64_t keep_protocol_thread_id, kill_type_t type);
    void execute_kill_user(const char* user, kill_type_t type);
    void execute_kill(std::shared_ptr<KillInfo> info, std::function<void()> cb);
    void send_ok_for_kill();
    void track_current_command(const mxs::Buffer& buf);
    void update_sequence(GWBUF* buf);
    bool large_query_continues(const mxs::Buffer& buffer) const;

    bool require_ssl() const;
    void update_user_account_entry();

    const MariaDBUserCache* user_account_cache();

    enum class AuthErrorType
    {
        ACCESS_DENIED,
        DB_ACCESS_DENIED,
        BAD_DB,
        NO_PLUGIN,
    };

    void   send_authentication_error(AuthErrorType error, const std::string& auth_mod_msg = "");
    void   send_misc_error(const std::string& msg);
    int    send_auth_error(int packet_number, const char* mysql_message);
    int    send_standard_error(int packet_number, int error_number, const char* error_message);
    GWBUF* create_standard_error(int sequence, int error_number, const char* msg);
    void   write_ok_packet(int sequence, uint8_t affected_rows = 0, const char* message = nullptr);

    // General connection state
    enum class State
    {
        HANDSHAKING,
        AUTHENTICATING,
        CHANGING_USER,
        READY,
        FAILED,
        QUIT,
    };

    // Handshake state
    enum class HSState
    {
        INIT,           /**< Initial handshake state */
        EXPECT_SSL_REQ, /**< Expecting client to send SSLRequest */
        SSL_NEG,        /**< Negotiate SSL*/
        EXPECT_HS_RESP, /**< Expecting client to send standard handshake response */
        COMPLETE,       /**< Handshake succeeded */
        FAIL,           /**< Handshake failed */
    };

    // Authentication state
    enum class AuthState
    {
        FIND_ENTRY,         /**< Find user account entry */
        TRY_AGAIN,          /**< Find user entry again with new data */
        NO_PLUGIN,          /**< Requested plugin is not loaded */
        START_EXCHANGE,     /**< Begin authenticator module exchange */
        CONTINUE_EXCHANGE,  /**< Continue exchange */
        CHECK_TOKEN,        /**< Check token against user account entry */
        START_SESSION,      /**< Start routing session */
        CHANGE_USER_OK,     /**< User-change processed */
        FAIL,               /**< Authentication failed */
        COMPLETE,           /**< Authentication is complete */
    };

    enum class SSLState
    {
        NOT_CAPABLE,
        INCOMPLETE,
        COMPLETE,
        FAIL
    };

    enum class RoutingState
    {
        PACKET_START,   /**< Expecting the client to send a normal packet */
        LARGE_PACKET,   /**< Expecting the client to continue streaming a large packet */
        LOAD_DATA,      /**< Expecting the client to continue streaming CSV-data */
    };

    /** Temporary data required during COM_CHANGE_USER. */
    struct ChangeUserFields
    {
        mxs::Buffer                    client_query;    /**< The original change-user-query from client. */
        std::unique_ptr<MYSQL_session> session;         /**< Temporary session-data */
    };

    SSLState ssl_authenticate_check_status();
    int      ssl_authenticate_client();

    State        m_state {State::HANDSHAKING};                  /**< Overall state */
    HSState      m_handshake_state {HSState::INIT};             /**< Handshake state */
    AuthState    m_auth_state {AuthState::FIND_ENTRY};          /**< Authentication state */
    RoutingState m_routing_state {RoutingState::PACKET_START};  /**< Routing state */

    mariadb::SClientAuth m_authenticator;   /**< Client authentication data */
    ChangeUserFields     m_change_user;     /**< User account to change to */

    mxs::Component* m_downstream {nullptr}; /**< Downstream component, the session */
    MXS_SESSION*    m_session {nullptr};    /**< Generic session */
    MYSQL_session*  m_session_data {nullptr};
    qc_sql_mode_t   m_sql_mode {QC_SQL_MODE_DEFAULT};   /**< SQL-mode setting */
    uint8_t         m_sequence {0};                     /**< Latest sequence number from client */
    uint8_t         m_command {0};
    uint64_t        m_version {0};                  /**< Numeric server version */

    bool m_user_update_wakeup {false};      /**< Waking up because of user account update? */
    int  m_previous_userdb_version {0};     /**< Userdb version used for first user account search */

    std::vector<std::unique_ptr<LocalClient>> m_local_clients;

    /**
     * Send an error packet to the client.
     *
     * @param packet_number Sequence number
     * @param in_affected_rows Affected rows
     * @param mysql_errno Error number
     * @param sqlstate_msg MySQL state message
     * @param mysql_message Error message
     * @return True on success
     */
    bool send_mysql_err_packet(int packet_number, int in_affected_rows,
                               int mysql_errno, const char* sqlstate_msg, const char* mysql_message);
};
