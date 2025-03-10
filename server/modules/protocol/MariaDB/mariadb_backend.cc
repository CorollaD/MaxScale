/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/protocol/mariadb/backend_connection.hh>

#include <arpa/inet.h>
#include <openssl/rand.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxbase/format.hh>
#include <maxbase/proxy_protocol.hh>
#include <maxscale/clock.hh>
#include <maxscale/listener.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/authenticator.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include "user_data.hh"

// For setting server status through monitor
#include "../../../core/internal/monitormanager.hh"

using mxs::ReplyState;
using std::string;
using std::move;

namespace
{
using Iter = MariaDBBackendConnection::Iter;

void skip_encoded_int(Iter& it)
{
    switch (*it)
    {
    case 0xfc:
        it += 3;
        break;

    case 0xfd:
        it += 4;
        break;

    case 0xfe:
        it += 9;
        break;

    default:
        ++it;
        break;
    }
}

uint64_t get_encoded_int(Iter& it)
{
    uint64_t len = *it++;

    switch (len)
    {
    case 0xfc:
        len = mariadb::get_byte2(it);
        it += 2;
        break;

    case 0xfd:
        len = mariadb::get_byte3(it);
        it += 3;
        break;

    case 0xfe:
        len = mariadb::get_byte8(it);
        it += 8;
        break;

    default:
        break;
    }

    return len;
}

std::string get_encoded_str(Iter& it)
{
    uint64_t len = get_encoded_int(it);
    auto start = it;
    it += len;
    return std::string(start, it);
}

std::string_view get_encoded_str_sv(Iter& it)
{
    uint64_t len = get_encoded_int(it);
    auto start = it;
    it += len;

    // TODO: Change this to std::contiguous_iterator_tag in C++20
    static_assert(std::is_same_v<std::iterator_traits<Iter>::iterator_category,
                                 std::random_access_iterator_tag>);
    mxb_assert_message(&*start + len == &*it, "Memory must be contiguous");

    return std::string_view(reinterpret_cast<const char*>(&*start), len);
}

void skip_encoded_str(Iter& it)
{
    auto len = get_encoded_int(it);
    it += len;
}
}

/**
 * Construct a detached backend connection. Session and authenticator attached separately.
 */
MariaDBBackendConnection::MariaDBBackendConnection(SERVER& server)
    : m_server(server)
    , m_auth_data(server.name())
{
}

/*******************************************************************************
 *******************************************************************************
 *
 * API Entry Point - Connect
 *
 * This is the first entry point that will be called in the life of a backend
 * (database) connection. It creates a protocol data structure and attempts
 * to open a non-blocking socket to the database. If it succeeds, the
 * protocol_auth_state will become MYSQL_CONNECTED.
 *
 *******************************************************************************
 ******************************************************************************/

std::unique_ptr<MariaDBBackendConnection>
MariaDBBackendConnection::create(MXS_SESSION* session, mxs::Component* component, SERVER& server)
{
    std::unique_ptr<MariaDBBackendConnection> backend_conn(new MariaDBBackendConnection(server));
    backend_conn->assign_session(session, component);
    backend_conn->pin_history_responses();
    return backend_conn;
}

void MariaDBBackendConnection::finish_connection()
{
    mxb_assert(m_dcb->handler());

    if (m_state != State::POOLED)
    {
        mysql_session()->history_info.erase(this);
    }

    // Always send a COM_QUIT to the backend being closed. This causes the connection to be closed faster.
    m_dcb->silence_errors();
    m_dcb->writeq_append(mysql_create_com_quit(nullptr, 0));
}

uint64_t MariaDBBackendConnection::can_reuse(MXS_SESSION* session) const
{
    MYSQL_session* data = static_cast<MYSQL_session*>(session->protocol_data());

    const uint64_t RELEVANT_CAPS = GW_MYSQL_CAPABILITIES_DEPRECATE_EOF | GW_MYSQL_CAPABILITIES_MULTI_RESULTS
        | GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS | GW_MYSQL_CAPABILITIES_SESSION_TRACK
        | GW_MYSQL_CAPABILITIES_PS_MULTI_RESULTS | MXS_EXTRA_CAPS_SERVER64;

    // The relevant capability bits that change how the protocol works must match with the ones used by this
    // session. Some of them, like the connection attributes, aren't relevant as the connection has already
    // been created.
    bool caps_ok = (m_capabilities & RELEVANT_CAPS) == (data->full_capabilities() & RELEVANT_CAPS);

    // If proxy_protocol is enabled, the client IP address must exactly match the one that was used to create
    // this connection. This prevents sharing of the same connection between different user accounts.
    bool remote_ok = !m_server.proxy_protocol() || m_dcb->client_remote() == session->client_remote();

    uint64_t rv = REUSE_NOT_POSSIBLE;

    if (caps_ok && remote_ok)
    {
        rv = ReuseType::CHANGE_USER;

        if (m_account == session->user_and_host() && m_db == data->current_db)
        {
            rv = ReuseType::RESET_CONNECTION;
        }
    }

    return rv;
}

bool MariaDBBackendConnection::reuse(MXS_SESSION* session, mxs::Component* upstream, uint64_t reuse_type)
{
    bool rv = false;
    mxb_assert(!m_dcb->session() && m_dcb->readq_empty() && m_dcb->writeq_empty());

    if (m_dcb->state() != DCB::State::POLLING || m_state != State::POOLED || !m_delayed_packets.empty())
    {
        MXB_INFO("DCB and protocol state do not qualify for reuse: %s, %s, %s",
                 mxs::to_string(m_dcb->state()), to_string(m_state).c_str(),
                 m_delayed_packets.empty() ? "no packets" : "stored packets");
    }
    else
    {
        assign_session(session, upstream);
        m_dcb->reset(session);

        bool reset_conn = reuse_type == ReuseType::RESET_CONNECTION;
        GWBUF buffer = reset_conn ? create_reset_connection_packet() : create_change_user_packet();

        /**
         * This is a connection that was just taken out of the persistent connection pool.
         * Send a COM_CHANGE_USER query to the backend to reset the session state. */
        if (m_dcb->writeq_append(std::move(buffer)))
        {
            MXB_INFO("Reusing connection, sending %s",
                     reset_conn ? "COM_RESET_CONNECTION" : "COM_CHANGE_USER");
            m_state = State::RESET_CONNECTION;
            rv = true;

            // Clear out any old prepared statements, those are reset by the COM_CHANGE_USER
            m_ps_map.clear();
            pin_history_responses();

            if (reset_conn && m_session->listener_data()->m_conn_init_sql.buffer_contents.empty())
            {
                // We don't have any initialization queries. This means we can send the history without having
                // to wait for the server's response as we know that the COM_RESET_CONNECTION will send only
                // one packet. This can't be done with a COM_CHANGE_USER as the server might respond with an
                // AuthSwitchRequest packet.
                m_state = State::RESET_CONNECTION_FAST;
                send_history();
            }
        }
    }

    return rv;
}

/**
 * @brief Log handshake failure
 *
 * @param dcb Backend DCB where authentication failed
 * @param buffer Buffer containing the response from the backend
 */
void MariaDBBackendConnection::handle_error_response(DCB* plain_dcb, GWBUF* buffer)
{
    mxb_assert(plain_dcb->role() == DCB::Role::BACKEND);
    BackendDCB* dcb = static_cast<BackendDCB*>(plain_dcb);
    uint16_t errcode = mxs_mysql_get_mysql_errno(buffer);
    std::string reason = mxs::extract_error(buffer);
    std::string errmsg = mxb::string_printf(
        "Authentication to '%s' failed: %hu, %s",
        dcb->server()->name(), errcode, reason.c_str());

    if (m_session->service->config()->log_auth_warnings)
    {
        MXB_ERROR("%s", errmsg.c_str());
    }

    /** If the error is ER_HOST_IS_BLOCKED put the server into maintenance mode.
     * This will prevent repeated authentication failures. */
    if (errcode == ER_HOST_IS_BLOCKED)
    {
        auto main_worker = mxs::MainWorker::get();
        auto server = dcb->server();
        main_worker->execute([server]() {
            MonitorManager::set_server_status(server, SERVER_MAINT);
        }, mxb::Worker::EXECUTE_AUTO);

        MXB_ERROR("Server %s has been put into maintenance mode due to the server blocking connections "
                  "from MaxScale. Run 'mysqladmin -h %s -P %d flush-hosts' on this server before taking "
                  "this server out of maintenance mode. To avoid this problem in the future, set "
                  "'max_connect_errors' to a larger value in the backend server.",
                  server->name(), server->address(), server->port());
    }
    else if (errcode == ER_ACCESS_DENIED_ERROR)
    {
        m_session->service->stats().add_failed_auth();

        // Authentication to backend failed. MaxScale must be operating on old user account data. This
        // session will fail, but update account data.
        auto user_cache = user_account_cache();
        if (user_cache)
        {
            if (user_cache->can_update_immediately())
            {
                m_session->service->request_user_account_update();
            }
            else
            {
                MXB_WARNING(USERS_RECENTLY_UPDATED_FMT, m_session->user_and_host().c_str());
            }
        }
        // If user cache does not exist, do nothing.
    }

    auto error_type = mxs::ErrorType::PERMANENT;

    // XPand responds with this sort of an authentication failure error while it's doing a group change. To
    // avoid permanently closing the backends, treat it as a transient error.
    if (errcode == 1 && reason.find("Group change during GTM operation") != std::string::npos)
    {
        error_type = mxs::ErrorType::TRANSIENT;
    }

    do_handle_error(m_dcb, errmsg, error_type);
}

/**
 * @brief Prepare protocol for a write
 *
 * This prepares both the buffer and the protocol itself for writing a query
 * to the backend.
 *
 * @param buffer Buffer that will be written
 */
void MariaDBBackendConnection::prepare_for_write(const GWBUF& buffer)
{
    if (m_session->capabilities() & RCAP_TYPE_REQUEST_TRACKING)
    {
        TrackedQuery query(buffer);

        if (m_reply.state() == ReplyState::DONE && m_track_queue.empty())
        {
            track_query(query);
        }
        else
        {
            m_track_queue.push(std::move(query));
        }
    }

    // TODO: These probably should be stored in TrackedQuery as well
    if (buffer.type_is_collect_result())
    {
        m_collect_result = true;
    }
}

void MariaDBBackendConnection::process_stmt_execute(GWBUF& original, uint32_t id, PSInfo& ps_info)
{
    // Only prepared statements with input parameters send metadata with COM_STMT_EXECUTE
    if (ps_info.n_params > 0 && !ps_info.exec_metadata_sent)
    {
        size_t types_offset = MYSQL_HEADER_LEN + 1 + 4 + 1 + 4 + ((ps_info.n_params + 7) / 8);
        uint8_t* ptr = original.data() + types_offset;

        if (*ptr == 0)
        {
            MYSQL_session* data = static_cast<MYSQL_session*>(m_session->protocol_data());
            auto it = data->exec_metadata.find(id);

            // Although this check is practically always true, it will prevent a broken
            // connector from crashing MaxScale.
            if (it != data->exec_metadata.end())
            {
                const auto& metadata = it->second;

                auto newsize = original.length() + metadata.size();
                GWBUF newbuf(newsize);
                newbuf.write_complete(newsize);
                auto dataptr = newbuf.data();

                memcpy(dataptr, original.data(), types_offset);
                dataptr += types_offset;

                // Set to 1, we are sending the types
                *dataptr++ = 1;

                // Splice the metadata into COM_STMT_EXECUTE
                memcpy(dataptr, metadata.data(), metadata.size());
                dataptr += metadata.size();

                // Copy remaining data that is being sent and update the packet length
                mxb_assert(original.length() > types_offset + 1);
                memcpy(dataptr, original.data() + types_offset + 1, original.length() - types_offset - 1);
                mariadb::set_byte3(newbuf.data(), newbuf.length() - MYSQL_HEADER_LEN);

                original = std::move(newbuf);
                ps_info.exec_metadata_sent = true;
            }
            else
            {
                mxb_assert_message(ps_info.n_params > 0, "Only PS with params can be malformed");
                MXB_WARNING("Malformed COM_STMT_EXECUTE (ID %u): could not find previous "
                            "execution with metadata and current execution doesn't contain it", id);
            }
        }
        else
        {
            ps_info.exec_metadata_sent = true;
        }
    }
}

void MariaDBBackendConnection::ready_for_reading(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);     // The protocol should only handle its own events.

    bool state_machine_continue = true;
    while (state_machine_continue)
    {
        switch (m_state)
        {
        case State::HANDSHAKING:
            {
                auto hs_res = handshake();
                switch (hs_res)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    m_state = State::AUTHENTICATING;
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::AUTHENTICATING:
            {
                auto auth_res = authenticate();
                switch (auth_res)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    m_state = State::CONNECTION_INIT;
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::CONNECTION_INIT:
            {
                auto init_res = send_connection_init_queries();
                switch (init_res)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    m_state = State::SEND_HISTORY;
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::SEND_HISTORY:
            send_history();
            m_state = State::READ_HISTORY;
            break;

        case State::READ_HISTORY:
            {
                auto res = read_history_response();
                switch (res)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    m_state = State::SEND_DELAYQ;
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::SEND_DELAYQ:
            m_state = State::ROUTING;
            send_delayed_packets();
            break;

        case State::RESET_CONNECTION_FAST:
        case State::RESET_CONNECTION:
        case State::READ_CHANGE_USER:
            {
                auto res = read_change_user();

                switch (res)
                {
                case StateMachineRes::IN_PROGRESS:
                    state_machine_continue = false;
                    break;

                case StateMachineRes::DONE:
                    break;

                case StateMachineRes::ERROR:
                    m_state = State::FAILED;
                    break;
                }
            }
            break;

        case State::SEND_CHANGE_USER:
            normal_read();

            if (!expecting_reply())
            {
                // No more replies expected, generate and send the COM_CHANGE_USER.
                send_change_user_to_backend();
            }
            else
            {
                state_machine_continue = false;
            }
            break;

        case State::PINGING:
            read_com_ping_response();
            break;

        case State::PREPARE_PS:
            normal_read();

            // The reply must be complete and we must have no pending queries to track. If m_track_queue is
            // not empty, that means the current result is not for the COM_STMT_PREPARE but for a command that
            // was executed before it.
            if (m_reply.is_complete() && m_track_queue.empty())
            {
                // The state can change inside do_handle_error() as a result of a failed network read from the
                // DCB or a mismatch in the result of a command stored in the history.
                if (m_state != State::FAILED)
                {
                    m_state = State::ROUTING;
                    send_delayed_packets();
                }
            }

            state_machine_continue = false;
            break;

        case State::ROUTING:
            normal_read();
            // Normal read always consumes all data.
            state_machine_continue = false;
            break;

        case State::POOLED:
            mxb_assert(!true);      // Should not currently happen.
            m_state = State::FAILED;
            state_machine_continue = false;
            break;

        case State::FAILED:
            state_machine_continue = false;
            break;
        }
    }
}

void MariaDBBackendConnection::do_handle_error(DCB* dcb, const std::string& errmsg, mxs::ErrorType type)
{
    std::ostringstream ss(errmsg, std::ios_base::app);

    ss << " (" << m_server.name();

    if (int err = gw_getsockerrno(dcb->fd()))
    {
        ss << ": " << err << ", " << mxb_strerror(err);
    }
    else if (dcb->is_fake_event())
    {
        // Fake events should not have TCP socket errors
        ss << ": Generated event";
    }

    ss << ")";

    // Erase history info callback before we do the handleError call. This prevents it from being called while
    // the DCB is in the zombie queue.
    mysql_session()->history_info.erase(this);

    mxb_assert(!dcb->hanged_up());
    GWBUF* errbuf = mysql_create_custom_error(1, 0, ER_CONNECTION_KILLED, ss.str().c_str());

    MXB_AT_DEBUG(bool res = ) m_upstream->handleError(type, errbuf, nullptr, m_reply);

    mxb_assert_message(res || m_session->state() == MXS_SESSION::State::STOPPING,
                       "The session should be stopping when handleError fails");
    mxb_assert_message(!res || !dcb->is_open(),
                       "The DCB must not be open after a successful handleError call");

    m_state = State::FAILED;

    gwbuf_free(errbuf);
}

/**
 * @brief Check if a reply can be routed to the client
 *
 * @param Backend DCB
 * @return True if session is ready for reply routing
 */
bool MariaDBBackendConnection::session_ok_to_route(DCB* dcb)
{
    bool rval = false;
    auto session = dcb->session();
    if (session->state() == MXS_SESSION::State::STARTED)
    {
        ClientDCB* client_dcb = session->client_connection()->dcb();
        if (client_dcb && client_dcb->state() != DCB::State::DISCONNECTED)
        {
            auto client_protocol = client_dcb->protocol();
            if (client_protocol)
            {
                if (client_protocol->in_routing_state())
                {
                    rval = true;
                }
            }
        }
    }


    return rval;
}

static inline bool auth_change_requested(const GWBUF& buf)
{
    return mxs_mysql_get_command(&buf) == MYSQL_REPLY_AUTHSWITCHREQUEST
           && buf.length() > MYSQL_EOF_PACKET_LEN;
}

bool MariaDBBackendConnection::handle_auth_change_response(const GWBUF& reply, DCB* dcb)
{
    bool rval = false;

    if (strcmp((const char*)reply.data() + 5, DEFAULT_MYSQL_AUTH_PLUGIN) == 0)
    {
        /**
         * The server requested a change of authentication methods.
         * If we're changing the authentication method to the same one we
         * are using now, it means that the server is simply generating
         * a new scramble for the re-authentication process.
         */
        rval = send_mysql_native_password_response(reply, dcb);
    }

    return rval;
}

/**
 * With authentication completed, read new data and write to backend
 */
void MariaDBBackendConnection::normal_read()
{
    /** Ask what type of output the router/filter chain expects */
    MXS_SESSION* session = m_dcb->session();
    uint64_t capabilities = session->capabilities();
    capabilities |= mysql_session()->client_protocol_capabilities();
    bool need_complete_packets = rcap_type_required(capabilities, RCAP_TYPE_PACKET_OUTPUT)
        || rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT)
        || rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT)
        || m_collect_result;

    // Limit the amount of data read so that client dcb writeq won't heavily exceed writeq_high_water.
    auto high_water_limit = config_writeq_high_water();

    size_t bytes_to_read = 0;
    if (high_water_limit > 0)
    {
        bytes_to_read = high_water_limit + 1;
        auto client_writeq_len = m_session->client_dcb->writeq_len();
        if (client_writeq_len < bytes_to_read)
        {
            bytes_to_read -= client_writeq_len;
        }
        else
        {
            // Should not really read anything. But logic may not handle that right now so read
            // at least a little.
            bytes_to_read = MYSQL_HEADER_LEN;
        }

        if (need_complete_packets)
        {
            uint8_t header_data[MYSQL_HEADER_LEN];
            // Existing packet can override the limit. This ensures that complete packets can be read even if
            // those packets are larger than writeq_high_water.
            if (m_dcb->readq_peek(MYSQL_HEADER_LEN, header_data) == MYSQL_HEADER_LEN)
            {
                auto curr_packet_len = mariadb::get_packet_length(header_data);
                if (curr_packet_len > bytes_to_read)
                {
                    bytes_to_read = curr_packet_len;
                }
            }
        }

        // In case user has configured a tiny writeq_high_water. This should be disallowed, though.
        bytes_to_read = std::max(bytes_to_read, (size_t)MYSQL_HEADER_LEN);
    }

    auto [read_ok, buffer] = m_dcb->read(MYSQL_HEADER_LEN, bytes_to_read);

    if (buffer.empty())
    {
        if (read_ok)
        {
            return;
        }
        else
        {
            do_handle_error(m_dcb, "Read from backend failed");
            return;
        }
    }

    bool result_collected = false;

    if (rcap_type_required(capabilities, RCAP_TYPE_PACKET_OUTPUT) || m_collect_result)
    {
        GWBUF tmp;
        bool track = rcap_type_required(capabilities, RCAP_TYPE_REQUEST_TRACKING)
            && !rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT);

        if (track || m_collect_result)
        {
            tmp = track_response(buffer);
        }
        else
        {
            tmp = mariadb::get_complete_packets(buffer);
        }

        // Store any partial packets in the DCB's read buffer
        if (!buffer.empty())
        {
            m_dcb->unread(std::move(buffer));

            if (m_reply.is_complete())
            {
                // There must be more than one response in the buffer which we need to process once we've
                // routed this response.
                m_dcb->trigger_read_event();
            }
        }

        if (tmp.empty())
        {
            return;     // No complete packets
        }

        buffer = std::move(tmp);
    }

    if (rcap_type_required(capabilities, RCAP_TYPE_RESULTSET_OUTPUT) || m_collect_result)
    {
        m_collectq.merge_back(std::move(buffer));

        if (!m_reply.is_complete())
        {
            return;
        }

        buffer = std::move(m_collectq);
        m_collect_result = false;
        result_collected = true;
    }

    do
    {
        GWBUF stmt;

        if (!result_collected && rcap_type_required(capabilities, RCAP_TYPE_STMT_OUTPUT))
        {
            // This happens if a session with RCAP_TYPE_STMT_OUTPUT closes the connection before all
            // the packets have been processed.
            if (!m_dcb->is_open())
            {
                buffer.clear();
                break;
            }

            // TODO: Get rid of RCAP_TYPE_STMT_OUTPUT and iterate over all packets in the resultset
            stmt = mariadb::get_next_MySQL_packet(buffer);
            mxb_assert_message(!stmt.empty(), "There should be only complete packets in buffer");

            GWBUF tmp = track_response(stmt);
            mxb_assert(stmt.empty());
            stmt = std::move(tmp);
        }
        else
        {
            stmt = std::move(buffer);
        }

        if (session_ok_to_route(m_dcb))
        {
            // This keeps the row data in the mxs::Reply valid for the whole clientReply call even if the
            // buffer is freed by a router or a filter.
            std::unique_ptr<GWBUF> sTmp_for_row_data;
            if (m_collect_rows)
            {
                sTmp_for_row_data.reset(new GWBUF(stmt));
            }

            thread_local mxs::ReplyRoute route;
            route.clear();
            m_upstream->clientReply(mxs::gwbuf_to_gwbufptr(std::move(stmt)), route, m_reply);
            m_reply.clear_row_data();
        }
        else
        {
            /*< session is closing; replying to client isn't possible */
        }
    }
    while (!buffer.empty());

    if (!m_dcb->is_open())
    {
        // The router closed the session, erase the callbacks to prevent the client protocol from calling it.
        mysql_session()->history_info.erase(this);
    }
    else if (rcap_type_required(capabilities, RCAP_TYPE_SESCMD_HISTORY) && m_reply.is_complete())
    {
        if (!compare_responses())
        {
            do_handle_error(m_dcb, create_response_mismatch_error(), mxs::ErrorType::PERMANENT);
        }
    }
}

void MariaDBBackendConnection::send_history()
{
    MYSQL_session* client_data = mysql_session();

    if (!client_data->history.empty())
    {
        for (const auto& history_query : client_data->history)
        {
            TrackedQuery query(history_query);

            if (m_reply.state() == ReplyState::DONE && m_track_queue.empty())
            {
                track_query(query);
            }
            else
            {
                m_track_queue.push(query);
            }

            MXB_INFO("Execute %s %u on '%s': %s", STRPACKETTYPE(query.command),
                     history_query.id(), m_server.name(), history_query.get_sql().c_str());

            m_dcb->writeq_append(GWBUF(history_query));
            m_history_responses.push_back(history_query.id());
        }
    }
}

MariaDBBackendConnection::StateMachineRes MariaDBBackendConnection::read_history_response()
{
    StateMachineRes rval = StateMachineRes::DONE;

    while (!m_history_responses.empty() && rval == StateMachineRes::DONE)
    {
        auto [read_ok, buffer] = m_dcb->read(MYSQL_HEADER_LEN, 0);

        if (buffer.empty())
        {
            if (read_ok)
            {
                rval = StateMachineRes::IN_PROGRESS;
            }
            else
            {
                do_handle_error(m_dcb, "Read from backend failed");
                rval = StateMachineRes::ERROR;
            }
        }
        else
        {
            track_response(buffer);
            if (!buffer.empty())
            {
                m_dcb->unread(std::move(buffer));
            }

            if (m_reply.is_complete())
            {
                MYSQL_session* client_data = mysql_session();
                uint32_t id = m_history_responses.front();
                auto it = client_data->history_responses.find(id);
                mxb_assert(it != client_data->history_responses.end());

                if (it != client_data->history_responses.end() && m_reply.is_ok() == it->second)
                {
                    MXB_INFO("Reply to %u complete", id);
                    m_history_responses.pop_front();
                }
                else
                {
                    // This server sent a different response than the one we sent to the client. Trigger a
                    // hangup event so that it is closed.
                    do_handle_error(m_dcb, create_response_mismatch_error(), mxs::ErrorType::PERMANENT);
                    m_dcb->trigger_hangup_event();
                    rval = StateMachineRes::ERROR;
                }
            }
            else
            {
                // The result is not yet complete. In practice this only happens with a COM_STMT_PREPARE that
                // has multiple input/output parameters.
                rval = StateMachineRes::IN_PROGRESS;
            }
        }
    }

    return rval;
}

std::string MariaDBBackendConnection::create_response_mismatch_error()
{
    std::ostringstream ss;

    ss << "Response from server '" << m_server.name() << "' "
       << "differs from the expected response to " << STRPACKETTYPE(m_reply.command()) << ". "
       << "Closing connection due to inconsistent session state.";

    if (m_reply.error())
    {
        ss << " Error: " << m_reply.error().message();
    }

    return ss.str();
}

void MariaDBBackendConnection::pin_history_responses()
{
    // Mark the start of the history responses that we're interested in. This guarantees that all responses
    // remain in effect while the connection reset is ongoing. This is needed to correctly detect a
    // COM_STMT_CLOSE that arrives after the connection creation and which caused the history to shrink.
    MYSQL_session* client_data = mysql_session();

    if (!client_data->history.empty())
    {
        client_data->history_info[this].position = client_data->history.front().id();
    }
}

bool MariaDBBackendConnection::compare_responses()
{
    mxb_assert(rcap_type_required(m_session->capabilities(), RCAP_TYPE_SESCMD_HISTORY));
    MYSQL_session* data = mysql_session();

    if (m_current_id)
    {
        // It's possible that there's already a response for this command. This can happen if the session
        // command is executed multiple times before the accepted answer has arrived. We only care about
        // the latest result.
        m_ids_to_check[m_current_id] = m_reply.is_ok();

        // Reset the ID after storing it to make sure debug assertions will catch any cases where a PS
        // response is read without a pre-assigned ID.
        m_current_id = 0;
    }

    // It is possible that the same command is verified twice if the session command ends up being executed
    // more than once. This happens as each failed attempt to execute it causes the response callback to be
    // installed and if the accepted answer arrives before the final response for this backend arrives, the
    // latest completed response from this backend is used. This can cause the connection to be closed even if
    // it would be considered valid later on.
    //
    // TODO: We could probably use m_current_id to prevent this from happening.

    bool ok = true;
    bool found = false;
    auto it = m_ids_to_check.begin();

    while (it != m_ids_to_check.end())
    {
        auto response_it = data->history_responses.find(it->first);

        if (response_it != data->history_responses.end())
        {
            data->history_info[this].position = it->first;

            if (it->second != response_it->second)
            {
                ok = false;
                break;
            }

            it = m_ids_to_check.erase(it);
            found = true;
        }
        else
        {
            ++it;
        }
    }

    if (ok && !found && !m_ids_to_check.empty())
    {
        data->history_info[this].response_cb = [this]() {
            if (!compare_responses())
            {
                do_handle_error(m_dcb, create_response_mismatch_error(),
                                mxs::ErrorType::PERMANENT);
            }
        };
    }

    return ok;
}

MariaDBBackendConnection::StateMachineRes MariaDBBackendConnection::read_change_user()
{
    auto [read_ok, buffer] = mariadb::read_protocol_packet(m_dcb);

    if (buffer.empty())
    {
        if (read_ok)
        {
            return StateMachineRes::IN_PROGRESS;
        }
        else
        {
            do_handle_error(m_dcb, "Read from backend failed");
            return StateMachineRes::ERROR;
        }
    }

    auto rv = StateMachineRes::ERROR;

    if (auth_change_requested(buffer) && handle_auth_change_response(buffer, m_dcb))
    {
        rv = StateMachineRes::IN_PROGRESS;
    }
    else
    {
        int cmd = mxs_mysql_get_command(&buffer);

        if (m_state == State::READ_CHANGE_USER)
        {
            // The COM_CHANGE_USER is now complete. The reply state must be updated here as the normal
            // result processing code doesn't deal with the COM_CHANGE_USER responses.
            set_reply_state(ReplyState::DONE);

            mxs::ReplyRoute route;
            m_reply.clear();
            m_reply.set_is_ok(cmd == MYSQL_REPLY_OK);
            if (m_upstream->clientReply(mxs::gwbuf_to_gwbufptr(move(buffer)), route, m_reply))
            {
                // If packets were received from the router while the COM_CHANGE_USER was in progress,
                // they are stored in the same delayed queue that is used for the initial connection.
                m_state = State::SEND_DELAYQ;
                rv = StateMachineRes::DONE;
            }
            else
            {
                rv = StateMachineRes::ERROR;
            }
        }
        else if (m_state == State::RESET_CONNECTION || m_state == State::RESET_CONNECTION_FAST)
        {
            if (cmd == MYSQL_REPLY_ERR)
            {
                std::string errmsg = "Failed to reuse connection: " + mxs::extract_error(&buffer);
                do_handle_error(m_dcb, errmsg, mxs::ErrorType::PERMANENT);
                rv = StateMachineRes::ERROR;
            }
            else
            {
                MXB_INFO("Connection reset complete");
                // Connection is being attached to a new session, so all initializations must be redone.
                m_state = m_state == State::RESET_CONNECTION_FAST ?
                    State::READ_HISTORY : State::CONNECTION_INIT;

                rv = StateMachineRes::DONE;
            }
        }
        else
        {
            mxb_assert(!true);
        }
    }

    return rv;
}

void MariaDBBackendConnection::read_com_ping_response()
{
    auto [read_ok, buffer] = mariadb::read_protocol_packet(m_dcb);
    if (buffer.empty())
    {
        if (!read_ok)
        {
            do_handle_error(m_dcb, "Failed to read COM_PING response");
        }
    }
    else
    {
        mxb_assert(mxs_mysql_get_command(&buffer) == MYSQL_REPLY_OK);
        // Route any packets that were received while we were pinging the backend
        m_state = m_delayed_packets.empty() ? State::ROUTING : State::SEND_DELAYQ;
    }
}

void MariaDBBackendConnection::write_ready(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    auto dcb = m_dcb;
    if (dcb->state() != DCB::State::POLLING)
    {
        /** Don't write to backend if backend_dcb is not in poll set anymore */
        const uint8_t* data = NULL;
        bool com_quit = false;

        if (!dcb->writeq_empty())
        {
            data = dcb->writeq().data();
            com_quit = MYSQL_IS_COM_QUIT(data);
        }

        if (data)
        {
            if (!com_quit)
            {
                MXB_ERROR("Attempt to write buffered data to backend failed due internal inconsistent "
                          "state: %s", mxs::to_string(dcb->state()));
            }
        }
        else
        {
            MXB_DEBUG("Dcb %p in state %s but there's nothing to write either.",
                      dcb, mxs::to_string(dcb->state()));
        }
    }
    else
    {
        if (m_state == State::HANDSHAKING && m_hs_state == HandShakeState::SEND_PROHY_HDR)
        {
            // Write ready is usually the first event delivered after a connection is made.
            // Proxy header should be sent in case the server is waiting for it.
            if (m_server.proxy_protocol())
            {
                m_hs_state = (send_proxy_protocol_header()) ? HandShakeState::EXPECT_HS :
                    HandShakeState::FAIL;
            }
            else
            {
                m_hs_state = HandShakeState::EXPECT_HS;
            }
        }
        dcb->writeq_drain();
    }
}

/*
 * Write function for backend DCB. Store command to protocol.
 *
 * @param queue Queue of buffers to write
 * @return      0 on failure, 1 on success
 */
int32_t MariaDBBackendConnection::write(GWBUF* queue)
{
    return write(mxs::gwbufptr_to_gwbuf(queue)) ? 1 : 0;
}

bool MariaDBBackendConnection::write(GWBUF&& queue)
{
    int rc = 0;
    switch (m_state)
    {
    case State::FAILED:
        if (m_session->state() != MXS_SESSION::State::STOPPING)
        {
            MXB_ERROR("Unable to write to backend '%s' because connection has failed. Server in state %s.",
                      m_server.name(), m_server.status_string().c_str());
        }

        rc = 0;
        break;

    case State::ROUTING:
        {
            // If the buffer contains a large query, we have to ignore the command byte and just write it. The
            // state of m_large_query must be updated for each routed packet to accurately know whether the
            // command byte is accurate or not.
            bool was_large = m_large_query;
            uint32_t packet_len = mariadb::get_header(queue.data()).pl_length;
            m_large_query = packet_len == MYSQL_PACKET_LENGTH_MAX;

            if (was_large || m_reply.state() == ReplyState::LOAD_DATA)
            {
                if (packet_len == 0 && m_reply.state() == ReplyState::LOAD_DATA)
                {
                    // An empty packet is sent at the end of the LOAD DATA LOCAL INFILE. Any packets received
                    // after this but before the server responds with the result should go through the normal
                    // code paths.
                    set_reply_state(ReplyState::LOAD_DATA_END);
                }

                // Not the start of a packet, don't analyze it.
                return m_dcb->writeq_append(std::move(queue));
            }

            uint8_t cmd = mxs_mysql_get_command(&queue);

            if (cmd == MXS_COM_CHANGE_USER)
            {
                // Discard the packet, we'll generate our own when we send it.
                if (expecting_reply())
                {
                    // Busy with something else, wait for it to complete and then send the COM_CHANGE_USER.
                    m_state = State::SEND_CHANGE_USER;
                    return 1;
                }
                else
                {
                    return send_change_user_to_backend();
                }
            }

            prepare_for_write(queue);

            if (mxs_mysql_is_ps_command(cmd))
            {
                uint32_t ps_id = mxs_mysql_extract_ps_id(&queue);
                auto it = m_ps_map.find(ps_id);

                if (it != m_ps_map.end())
                {
                    // Ensure unique GWBUF to prevent our modification of the PS ID from
                    // affecting the original buffer.
                    queue.ensure_unique();

                    // Replace our generated ID with the real PS ID
                    uint8_t* ptr = queue.data() + MYSQL_PS_ID_OFFSET;
                    mariadb::set_byte4(ptr, it->second.real_id);

                    if (cmd == MXS_COM_STMT_CLOSE)
                    {
                        m_ps_map.erase(it);
                    }
                    else if (cmd == MXS_COM_STMT_EXECUTE)
                    {
                        process_stmt_execute(queue, ps_id, it->second);
                    }
                }
                else if (ps_id != MARIADB_PS_DIRECT_EXEC_ID)
                {
                    std::stringstream ss;
                    ss << "Unknown prepared statement handler (" << ps_id << ") given to MaxScale for "
                       << STRPACKETTYPE(cmd) << " by " << m_session->user_and_host();

                    // Only send the error if the client expects a response. If an unknown COM_STMT_CLOSE is
                    // sent, don't respond to it.
                    if (cmd == MXS_COM_STMT_CLOSE)
                    {
                        auto data = mysql_session();

                        if (data->history_responses.find(ps_id) != data->history_responses.end())
                        {
                            // If we haven't executed the COM_STMT_PREPARE that this COM_STMT_CLOSE refers to
                            // but we have the response for it, we know that the COM_STMT_CLOSE was received
                            // after the connection was opened but before we reached the history replay state.
                            // This can be relied on as the history position is pinned to the lowest ID when
                            // the connection is opened.
                            return 1;
                        }
                    }
                    else
                    {
                        GWBUF* err = mysql_create_custom_error(
                            1, 0, ER_UNKNOWN_STMT_HANDLER, ss.str().c_str());

                        // Send the error as a separate event. This allows the routeQuery of the router to
                        // finish before we deliver the response.
                        // TODO: questionable code. Deliver the error in some other way.
                        mxb_assert(m_dcb->readq_empty());
                        m_dcb->unread(err);
                        m_dcb->trigger_read_event();
                    }

                    MXB_WARNING("%s", ss.str().c_str());

                    // This is an error condition that is very likely to happen if something is broken in the
                    // prepared statement handling. Asserting that we never get here when we're testing helps
                    // catch the otherwise hard to spot error. Since this code is expected to be hit in
                    // environments where a connector sends an unknown ID, we can't treat this as a hard error
                    // and close the session. The only known exception to this is the test for MXS-3392 which
                    // causes a COM_STMT_CLOSE with a zero ID to be sent.
                    mxb_assert(!true || (cmd == MXS_COM_STMT_CLOSE && ps_id == 0));
                    return 1;
                }
            }

            if (cmd == MXS_COM_QUIT && m_server.persistent_conns_enabled())
            {
                /** We need to keep the pooled connections alive so we just ignore the COM_QUIT packet */
                rc = 1;
            }
            else
            {
                if (cmd == MXS_COM_STMT_PREPARE)
                {
                    // Stop accepting new queries while a COM_STMT_PREPARE is in progress. This makes sure
                    // that it completes before other commands that refer to it are processed. This can happen
                    // when a COM_STMT_PREPARE is routed to multiple backends and a faster backend sends the
                    // response to the client. This means that while this backend is still busy executing it,
                    // a COM_STMT_CLOSE for the prepared statement can arrive.
                    m_state = State::PREPARE_PS;
                }

                /** Write to backend */
                rc = m_dcb->writeq_append(std::move(queue));
            }
        }
        break;

    case State::PREPARE_PS:
        {
            if (m_large_query)
            {
                // A continuation of a large COM_STMT_PREPARE
                auto hdr = mariadb::get_header(queue.data());
                m_large_query = hdr.pl_length == MYSQL_PACKET_LENGTH_MAX;
                rc = m_dcb->writeq_append(std::move(queue));
            }
            else
            {
                MXB_INFO("Storing %s while in state '%s': %s", STRPACKETTYPE(mxs_mysql_get_command(&queue)),
                         to_string(m_state).c_str(), queue.get_sql().c_str());
                m_delayed_packets.emplace_back(std::move(queue));
                rc = 1;
            }
        }
        break;

    default:
        {
            MXB_INFO("Storing %s while in state '%s': %s", STRPACKETTYPE(mxs_mysql_get_command(&queue)),
                     to_string(m_state).c_str(), queue.get_sql().c_str());
            m_delayed_packets.emplace_back(std::move(queue));
            rc = 1;
        }
        break;
    }
    return rc;
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error
 * handler fails in providing enough backend servers, mark session being
 * closed and call DCB close function which triggers closing router session
 * and related backends (if any exists.
 */
void MariaDBBackendConnection::error(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);

    const auto dcb_state = m_dcb->state();
    if (dcb_state != DCB::State::POLLING || m_session->state() != MXS_SESSION::State::STARTED)
    {
        int error = 0;
        int len = sizeof(error);

        if (getsockopt(m_dcb->fd(), SOL_SOCKET, SO_ERROR, &error, (socklen_t*) &len) == 0 && error != 0)
        {
            MXB_ERROR("Network error in connection to server '%s', session in state '%s' (%s): %d, %s",
                      m_server.name(), session_state_to_string(m_session->state()), mxs::to_string(dcb_state),
                      error, mxb_strerror(error));
        }
    }
    else
    {
        do_handle_error(m_dcb, "Lost connection to backend server: network error");
    }
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error
 * handler fails in providing enough backend servers, mark session being
 * closed and call DCB close function which triggers closing router session
 * and related backends (if any exists.
 *
 * @param event_dcb The current Backend DCB
 * @return 1 always
 */
void MariaDBBackendConnection::hangup(DCB* event_dcb)
{
    mxb_assert(m_dcb == event_dcb);
    mxb_assert(m_dcb->is_open());
    MXS_SESSION* session = m_dcb->session();
    mxb_assert(session);

    if (session->state() != MXS_SESSION::State::STARTED)
    {
        int error;
        int len = sizeof(error);
        if (getsockopt(m_dcb->fd(), SOL_SOCKET, SO_ERROR, &error, (socklen_t*) &len) == 0)
        {
            if (error != 0 && session->state() != MXS_SESSION::State::STOPPING)
            {
                MXB_ERROR("Network hangup in connection to server '%s', session in state '%s' (%s): %d, %s",
                          m_server.name(), session_state_to_string(m_session->state()),
                          mxs::to_string(m_dcb->state()), error, mxb_strerror(error));
            }
        }
    }
    else
    {
        do_handle_error(m_dcb, "Lost connection to backend server: connection closed by peer");
    }
}

GWBUF MariaDBBackendConnection::create_reset_connection_packet()
{
    uint8_t buf[] = {0x1, 0x0, 0x0, 0x0, MXS_COM_RESET_CONNECTION};
    return GWBUF(buf, sizeof(buf));
}

/**
 * Create COM_CHANGE_USER packet and store it to GWBUF.
 *
 * @return GWBUF buffer consisting of COM_CHANGE_USER packet
 * @note the function doesn't fail
 */
GWBUF MariaDBBackendConnection::create_change_user_packet()
{
    const auto& client_auth_data = *m_auth_data.client_data->auth_data;
    auto make_auth_token = [this, &client_auth_data] {
        std::vector<uint8_t> rval;
        const string& hex_hash2 = client_auth_data.user_entry.entry.password;
        if (hex_hash2.empty())
        {
            m_current_auth_token.clear();
            return rval;        // Empty password -> empty token
        }

        // Need to compute the value of:
        // SHA1(scramble || SHA1(SHA1(password))) ⊕ SHA1(password)

        // SHA1(SHA1(password)) is in the user entry and needs to be converted to binary form.
        if (hex_hash2.length() == 2 * SHA_DIGEST_LENGTH)
        {
            uint8_t hash2[SHA_DIGEST_LENGTH];
            mxs::hex2bin(hex_hash2.c_str(), hex_hash2.length(), hash2);

            // Calculate SHA1(CONCAT(scramble, hash2) */
            uint8_t concat_hash[SHA_DIGEST_LENGTH];
            gw_sha1_2_str(m_auth_data.scramble, MYSQL_SCRAMBLE_LEN, hash2, SHA_DIGEST_LENGTH,
                          concat_hash);

            // SHA1(password) was sent by client and is in binary form.
            auto& hash1 = client_auth_data.backend_token;
            if (hash1.size() == SHA_DIGEST_LENGTH)
            {
                m_current_auth_token = hash1;
                // Compute the XOR */
                uint8_t new_token[SHA_DIGEST_LENGTH];
                mxs::bin_bin_xor(concat_hash, hash1.data(), SHA_DIGEST_LENGTH, new_token);
                rval.assign(new_token, new_token + SHA_DIGEST_LENGTH);
            }
        }
        return rval;
    };

    auto mses = m_auth_data.client_data;
    std::vector<uint8_t> payload;
    payload.reserve(200);   // Enough for most cases.

    auto insert_stringz = [&payload](const std::string& str) {
        auto n = str.length() + 1;
        auto zstr = str.c_str();
        payload.insert(payload.end(), zstr, zstr + n);
    };

    // Command byte COM_CHANGE_USER 0x11 */
    payload.push_back(MXS_COM_CHANGE_USER);

    insert_stringz(client_auth_data.user);

    // Calculate the authentication token.
    auto token = make_auth_token();
    payload.push_back(token.size());
    payload.insert(payload.end(), token.begin(), token.end());

    insert_stringz(client_auth_data.default_db);

    uint8_t charset[2];
    mariadb::set_byte2(charset, client_auth_data.collation);
    payload.insert(payload.end(), charset, charset + sizeof(charset));

    insert_stringz(client_auth_data.plugin);
    auto& attr = client_auth_data.attributes;
    payload.insert(payload.end(), attr.begin(), attr.end());

    auto buflen = MYSQL_HEADER_LEN + payload.size();
    GWBUF buffer(buflen);
    buffer.write_complete(buflen);
    auto data = buffer.data();
    data = mariadb::write_header(data, payload.size(), 0);
    mariadb::copy_bytes(data, payload.data(), payload.size());
    // COM_CHANGE_USER is a session command so the result must be collected.
    buffer.set_type(GWBUF::TYPE_COLLECT_RESULT);

    return buffer;
}

/**
 * Write a MySQL CHANGE_USER packet to backend server.
 *
 * @return True on success
 */
bool MariaDBBackendConnection::send_change_user_to_backend()
{
    bool rval = false;
    if (m_dcb->writeq_append(create_change_user_packet()))
    {
        m_state = State::READ_CHANGE_USER;
        rval = true;
    }
    return rval;
}

/* Send proxy protocol header. See
 * http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt
 * for more information. Currently only supports the text version (v1) of
 * the protocol. Binary version may be added later.
 */
bool MariaDBBackendConnection::send_proxy_protocol_header()
{
    // The header contains the original client address and the backend server address.
    // Client dbc always exists, as it's only freed at session close.
    const ClientDCB* client_dcb = m_session->client_connection()->dcb();
    const auto& client_addr = client_dcb->ip();         // Client address was filled in by accept().

    // Fill in the target server's address.
    sockaddr_storage server_addr {};
    socklen_t server_addrlen = sizeof(server_addr);
    int res = getpeername(m_dcb->fd(), (sockaddr*)&server_addr, &server_addrlen);
    if (res != 0)
    {
        int eno = errno;
        MXB_ERROR("getpeername() failed on connection to '%s' when forming proxy protocol header. "
                  "Error %d: '%s'", m_server.name(), eno, mxb_strerror(eno));
        return false;
    }

    bool success = false;
    auto proxyhdr_res = mxb::proxy_protocol::gen_text_header(client_addr, server_addr);
    if (proxyhdr_res.errmsg.empty())
    {
        auto ptr = reinterpret_cast<uint8_t*>(proxyhdr_res.header);
        auto len = proxyhdr_res.len;
        MXB_INFO("Sending proxy-protocol header '%.*s' to server '%s'.",
                 (int)len - 2, proxyhdr_res.header, m_server.name());
        if (m_dcb->writeq_append(GWBUF(ptr, len)))
        {
            success = true;
        }
    }
    else
    {
        MXB_ERROR("%s", proxyhdr_res.errmsg.c_str());
    }

    return success;
}

bool MariaDBBackendConnection::established()
{
    return m_state == State::ROUTING && m_reply.is_complete();
}

void MariaDBBackendConnection::ping()
{
    mxb_assert(m_reply.state() == ReplyState::DONE);
    mxb_assert(is_idle());
    MXB_INFO("Pinging '%s', idle for %ld seconds", m_server.name(), m_dcb->seconds_idle());

    constexpr uint8_t com_ping_packet[] =
    {
        0x01, 0x00, 0x00, 0x00, 0x0e
    };

    if (m_dcb->writeq_append(GWBUF(com_ping_packet, sizeof(com_ping_packet))))
    {
        m_state = State::PINGING;
    }
}

bool MariaDBBackendConnection::can_close() const
{
    return m_state == State::ROUTING || m_state == State::FAILED;
}

bool MariaDBBackendConnection::is_idle() const
{
    return m_state == State::ROUTING
           && m_reply.state() == ReplyState::DONE
           && m_reply.command() != MXS_COM_STMT_SEND_LONG_DATA
           && m_track_queue.empty();
}

size_t MariaDBBackendConnection::sizeof_buffers() const
{
    size_t rv = 0;

    for (const auto& buffer : m_delayed_packets)
    {
        rv += buffer.runtime_size();
    }

    rv += (m_dcb ? m_dcb->runtime_size() : 0);

    return rv;
}

json_t* MariaDBBackendConnection::diagnostics() const
{
    return json_pack("{sissss}", "connection_id", m_thread_id, "server", m_server.name(),
                     "cipher", m_dcb->ssl_cipher().c_str());
}

/**
 * Process a reply from a backend server. This method collects all complete packets and
 * updates the internal response state.
 *
 * @param buffer Buffer containing the raw response. Any partial packets will be left in this buffer.
 * @return All complete packets that were in `buffer`
 */
GWBUF MariaDBBackendConnection::track_response(GWBUF& buffer)
{
    GWBUF rval = process_packets(buffer);
    if (!rval.empty())
    {
        m_reply.add_bytes(rval.length());
    }
    return rval;
}

/**
 * Read the backend server MySQL handshake
 *
 * @return true on success, false on failure
 */
bool MariaDBBackendConnection::read_backend_handshake(GWBUF&& buffer)
{
    bool rval = false;
    uint8_t* payload = buffer.data() + MYSQL_HEADER_LEN;

    if (gw_decode_mysql_server_handshake(payload) >= 0)
    {
        rval = true;
    }

    return rval;
}

bool MariaDBBackendConnection::capability_mismatch() const
{
    bool mismatch = false;

    if (use_deprecate_eof() && (server_capabilities & GW_MYSQL_CAPABILITIES_DEPRECATE_EOF) == 0)
    {
        // This is an unexpected situation but it can happen if the server is swapped out without MaxScale
        // recalculating the version. Mostly this is here to catch any possible bugs that there might be in
        // the capability handling of MaxScale. Separate code should exist for routers for any unexpected
        // responses as bugs in the server can cause mismatching result types to be sent,
        // https://bugs.mysql.com/bug.php?id=83346 is one example of such.
        MXB_INFO("Client uses DEPRECATE_EOF protocol but the server does not implement it");
        mxb_assert_message(!true, "DEPRECATE_EOF should be used by both client and backend");
        mismatch = true;
    }

    uint32_t client_extra = mysql_session()->extra_capabilities();

    if ((client_extra & extra_capabilities) != client_extra)
    {
        MXB_INFO("Client uses extended capabilities that the server does not implement: %u != %u",
                 client_extra, extra_capabilities);
        mxb_assert(!true);
        mismatch = true;
    }


    return mismatch;
}

/**
 * Sends a response for an AuthSwitchRequest to the default auth plugin
 */
int MariaDBBackendConnection::send_mysql_native_password_response(const GWBUF& reply, DCB* dcb)
{
    // Calculate the next sequence number
    auto header = mariadb::get_header(reply.data());
    auto seqno = header.seq + 1;

    // Copy the new scramble. Skip packet header, command byte and null-terminated plugin name.
    const char default_plugin_name[] = DEFAULT_MYSQL_AUTH_PLUGIN;
    reply.copy_data(MYSQL_HEADER_LEN + 1 + sizeof(default_plugin_name), sizeof(m_auth_data.scramble),
                    m_auth_data.scramble);

    const auto& sha1_pw = m_current_auth_token;
    const uint8_t* curr_passwd = sha1_pw.empty() ? null_client_sha1 : sha1_pw.data();

    GWBUF* buffer = gwbuf_alloc(MYSQL_HEADER_LEN + GW_MYSQL_SCRAMBLE_SIZE);
    uint8_t* data = GWBUF_DATA(buffer);
    mariadb::set_byte3(data, GW_MYSQL_SCRAMBLE_SIZE);
    data[3] = seqno;    // This is the third packet after the COM_CHANGE_USER
    mxs_mysql_calculate_hash(m_auth_data.scramble, curr_passwd, data + MYSQL_HEADER_LEN);

    return dcb->writeq_append(buffer);
}

/**
 * Decode mysql server handshake
 *
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 *
 */
int MariaDBBackendConnection::gw_decode_mysql_server_handshake(uint8_t* payload)
{
    auto conn = this;
    uint8_t* server_version_end = NULL;
    uint16_t mysql_server_capabilities_one = 0;
    uint16_t mysql_server_capabilities_two = 0;
    uint8_t scramble_data_1[GW_SCRAMBLE_LENGTH_323] = "";
    uint8_t scramble_data_2[GW_MYSQL_SCRAMBLE_SIZE - GW_SCRAMBLE_LENGTH_323] = "";
    uint8_t capab_ptr[4] = "";
    uint8_t scramble_len = 0;
    uint8_t mxs_scramble[GW_MYSQL_SCRAMBLE_SIZE] = "";
    int protocol_version = 0;

    protocol_version = payload[0];

    if (protocol_version != GW_MYSQL_PROTOCOL_VERSION)
    {
        return -1;
    }

    payload++;

    // Get server version (string)
    server_version_end = (uint8_t*) gw_strend((char*) payload);

    payload = server_version_end + 1;

    // get ThreadID: 4 bytes
    uint32_t tid = mariadb::get_byte4(payload);

    MXB_INFO("Connected to '%s' with thread id %u", m_server.name(), tid);

    /* TODO: Correct value of thread id could be queried later from backend if
     * there is any worry it might be larger than 32bit allows. */
    conn->m_thread_id = tid;

    payload += 4;

    // scramble_part 1
    memcpy(scramble_data_1, payload, GW_SCRAMBLE_LENGTH_323);
    payload += GW_SCRAMBLE_LENGTH_323;

    // 1 filler
    payload++;

    mysql_server_capabilities_one = mariadb::get_byte2(payload);

    // Get capabilities_part 1 (2 bytes) + 1 language + 2 server_status
    payload += 5;

    mysql_server_capabilities_two = mariadb::get_byte2(payload);

    conn->server_capabilities = mysql_server_capabilities_one | mysql_server_capabilities_two << 16;

    // 2 bytes shift
    payload += 2;

    // get scramble len
    if (payload[0] > 0)
    {
        scramble_len = std::min(payload[0] - 1, GW_MYSQL_SCRAMBLE_SIZE);
    }
    else
    {
        scramble_len = GW_MYSQL_SCRAMBLE_SIZE;
    }

    mxb_assert(scramble_len > GW_SCRAMBLE_LENGTH_323);
    // Skip the scramble length
    payload += 1;

    // skip 6 bytes of filler
    payload += 6;

    if ((conn->server_capabilities & GW_MYSQL_CAPABILITIES_CLIENT_MYSQL) == 0)
    {
        conn->extra_capabilities = mariadb::get_byte4(payload);
    }

    payload += 4;

    // copy the second part of the scramble
    memcpy(scramble_data_2, payload, scramble_len - GW_SCRAMBLE_LENGTH_323);

    memcpy(mxs_scramble, scramble_data_1, GW_SCRAMBLE_LENGTH_323);
    memcpy(mxs_scramble + GW_SCRAMBLE_LENGTH_323, scramble_data_2, scramble_len - GW_SCRAMBLE_LENGTH_323);

    // full 20 bytes scramble is ready
    memcpy(m_auth_data.scramble, mxs_scramble, GW_MYSQL_SCRAMBLE_SIZE);
    return 0;
}

/**
 * Create a response to the server handshake
 *
 * @param with_ssl             Whether to create an SSL response or a normal response packet
 * @param ssl_established      Set to true if the SSL response has been sent
 * @param service_capabilities Capabilities of the connecting service
 *
 * @return Generated response packet
 */
GWBUF MariaDBBackendConnection::gw_generate_auth_response(bool with_ssl, bool ssl_established,
                                                          uint64_t service_capabilities)
{
    auto client_data = m_auth_data.client_data;
    uint8_t client_capabilities[4] = {0, 0, 0, 0};
    const uint8_t* curr_passwd = NULL;

    if (client_data->auth_data->backend_token.size() == SHA_DIGEST_LENGTH)
    {
        curr_passwd = client_data->auth_data->backend_token.data();
    }

    const auto& default_db = client_data->auth_data->default_db;
    uint32_t capabilities = create_capabilities(with_ssl, service_capabilities);
    mariadb::set_byte4(client_capabilities, capabilities);

    /**
     * Use the default authentication plugin name. If the server is using a
     * different authentication mechanism, it will send an AuthSwitchRequest
     * packet.
     */
    const char* auth_plugin_name = DEFAULT_MYSQL_AUTH_PLUGIN;

    const std::string& username = client_data->auth_data->user;
    // TODO: Make this a member function, only MariaDBBackendConnection uses it
    long bytes = response_length(with_ssl,
                                 ssl_established,
                                 username.c_str(),
                                 curr_passwd,
                                 default_db.c_str(),
                                 auth_plugin_name);

    const auto& attrs = client_data->auth_data->attributes;
    if (!with_ssl || ssl_established)
    {
        if (capabilities & this->server_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_ATTRS)
        {
            bytes += attrs.size();
        }
    }

    // allocating the GWBUF
    GWBUF buffer(bytes);
    buffer.write_complete(bytes);
    uint8_t* payload = buffer.data();

    // clearing data
    memset(payload, '\0', bytes);

    // put here the paylod size: bytes to write - 4 bytes packet header
    mariadb::set_byte3(payload, (bytes - 4));

    // set packet # = 1
    payload[3] = ssl_established ? '\x02' : '\x01';
    payload += 4;

    // set client capabilities
    memcpy(payload, client_capabilities, 4);

    // set now the max-packet size
    payload += 4;
    mariadb::set_byte4(payload, 16777216);

    // set the charset
    payload += 4;
    *payload = client_data->auth_data->collation;

    payload++;

    // 19 filler bytes of 0
    payload += 19;

    // Either MariaDB 10.2 extra capabilities or 4 bytes filler
    uint32_t extra_capabilities = client_data->extra_capabilities();
    memcpy(payload, &extra_capabilities, sizeof(extra_capabilities));
    payload += 4;

    if (!with_ssl || ssl_established)
    {
        // 4 + 4 + 4 + 1 + 23 = 36, this includes the 4 bytes packet header
        memcpy(payload, username.c_str(), username.length());
        payload += username.length();
        payload++;

        if (curr_passwd)
        {
            payload = load_hashed_password(m_auth_data.scramble, payload, curr_passwd);
        }
        else
        {
            payload++;
        }

        // if the db is not NULL append it
        if (default_db[0])
        {
            memcpy(payload, default_db.c_str(), default_db.length());
            payload += default_db.length();
            payload++;
        }

        memcpy(payload, auth_plugin_name, strlen(auth_plugin_name));

        if ((capabilities & this->server_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_ATTRS)
            && !attrs.empty())
        {
            // Copy client attributes as-is. This allows us to pass them along without having to process them.
            payload += strlen(auth_plugin_name) + 1;
            memcpy(payload, attrs.data(), attrs.size());
        }
    }

    return buffer;
}

/**
 * @brief Computes the capabilities bit mask for connecting to backend DB
 *
 * We start by taking the default bitmask and removing any bits not set in
 * the bitmask contained in the connection structure. Then add SSL flag if
 * the connection requires SSL (set from the MaxScale configuration). The
 * compression flag may be set, although compression is NOT SUPPORTED. If a
 * database name has been specified in the function call, the relevant flag
 * is set.
 *
 * @param db_specified Whether the connection request specified a database
 * @param compress Whether compression is requested - NOT SUPPORTED
 * @return Bit mask (32 bits)
 * @note Capability bits are defined in maxscale/protocol/mysql.h
 */
uint32_t MariaDBBackendConnection::create_capabilities(bool with_ssl, uint64_t capabilities)
{
    uint32_t final_capabilities = m_auth_data.client_data->client_capabilities();

    // Disable the cert verification capability, it has never been enabled in MaxScale.
    // TODO: Figure out if this is correct, the documentation doesn't mention this capability at all
    final_capabilities &= ~GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT;

    if (with_ssl)
    {
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SSL;
    }
    else
    {
        final_capabilities &= ~GW_MYSQL_CAPABILITIES_SSL;
    }

    if (rcap_type_required(capabilities, RCAP_TYPE_SESSION_STATE_TRACKING))
    {
        /** add session track */
        final_capabilities |= (uint32_t)GW_MYSQL_CAPABILITIES_SESSION_TRACK;
    }

    // We need to enable the CONNECT_WITH_DB capability depending on whether a default database exists. We
    // can't rely on the client's capabilities as the default database might have changed when a
    // COM_CHANGE_USER is executed.
    if (!m_auth_data.client_data->auth_data->default_db.empty())
    {
        final_capabilities |= GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
    }
    else
    {
        final_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
    }

    // The current handshake response generation code assumes that the follwing capabilites are always
    // enabled. Since the plugin is always mysql_native_password, the AUTH_LELENC_DATA isn't really needed as
    // the auth data size is always 20 bytes and both the length-encoded string representation and the string
    // prefixed with fixed size integer representation are the same for payloads less than 251 bytes.
    //
    // TODO: Send a handshake response that the client's original capabilities would require
    final_capabilities |= GW_MYSQL_CAPABILITIES_PLUGIN_AUTH
        | GW_MYSQL_CAPABILITIES_SECURE_CONNECTION
        | GW_MYSQL_CAPABILITIES_AUTH_LENENC_DATA;


    if (rcap_type_required(capabilities, RCAP_TYPE_MULTI_STMT_SQL))
    {
        // Currently only readwritesplit requires this as it implements causal_reads with multi-statements.
        final_capabilities |= GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS | GW_MYSQL_CAPABILITIES_MULTI_RESULTS;
    }

    return final_capabilities;
}

GWBUF MariaDBBackendConnection::process_packets(GWBUF& result)
{
    GWBUF& buffer = result;
    auto it = buffer.begin();
    size_t total_bytes = buffer.length();
    size_t bytes_used = 0;

    while (it != buffer.end())
    {
        size_t bytes_left = total_bytes - bytes_used;
        if (bytes_left < MYSQL_HEADER_LEN)
        {
            // Partial header
            break;
        }

        // Extract packet length
        uint32_t len = mariadb::get_header(it).pl_length;
        if (bytes_left < len + MYSQL_HEADER_LEN)
        {
            // Partial packet payload
            break;
        }

        bytes_used += len + MYSQL_HEADER_LEN;
        it += MYSQL_HEADER_LEN;
        mxb_assert(it != buffer.end());
        auto end = it + len;

        // Ignore the tail end of a large packet. Only resultsets can generate packets this large
        // and we don't care what the contents are and thus it is safe to ignore it.
        bool skip_next = m_skip_next;
        m_skip_next = len == GW_MYSQL_MAX_PACKET_LEN;

        if (!skip_next)
        {
            process_one_packet(it, end, len);
        }

        it = end;

        if (m_reply.state() == ReplyState::DONE)
        {
            break;
        }
    }

    return result.split(bytes_used);
}

void MariaDBBackendConnection::process_one_packet(Iter it, Iter end, uint32_t len)
{
    uint8_t cmd = *it;
    switch (m_reply.state())
    {
    case ReplyState::START:
        process_reply_start(it, end);
        break;

    case ReplyState::LOAD_DATA:
        // This should not happen as the server is supposed to wait for the whole LOAD DATA LOCAL INFILE to
        // complete before sending a response. It is however possible that something else, for example another
        // MaxScale, causes an error to be sent even if the client hasn't finished sending the data.
        MXB_ERROR("Response to LOAD DATA LOCAL INFILE read before the upload was complete: "
                  "cmd: 0x%02hhx, len: %u, server: %s", cmd, len, m_server.name());
        mxb_assert(!true);
        /** Fallthrough */

    case ReplyState::LOAD_DATA_END:
        MXB_INFO("Load data ended on '%s'", m_server.name());

        if (cmd == MYSQL_REPLY_ERR)
        {
            update_error(++it, end);
            set_reply_state(ReplyState::DONE);
        }
        else if (cmd == MYSQL_REPLY_OK)
        {
            m_reply.set_is_ok(true);
            process_ok_packet(it, end);

            if (m_reply.state() != ReplyState::DONE)
            {
                // The LOAD DATA LOCAL INFILE completed but we're expecting more results. Go back to the START
                // state in order to process the next result.
                set_reply_state(ReplyState::START);
            }
        }
        else
        {
            MXB_ERROR("Unexpected response to LOAD DATA LOCAL INFILE: cmd: 0x%02hhx, len: %u, server: %s",
                      cmd, len, m_server.name());
            m_session->dump_statements();
            m_session->dump_session_log();
            m_session->kill();
            mxb_assert(!true);
        }
        break;

    case ReplyState::DONE:

        while (!m_track_queue.empty())
        {
            track_query(m_track_queue.front());
            m_track_queue.pop();

            if (m_reply.state() != ReplyState::DONE)
            {
                // There's another reply waiting to be processed, start processing it.
                process_one_packet(it, end, len);
                return;
            }
        }

        if (cmd == MYSQL_REPLY_ERR)
        {
            update_error(++it, end);
        }
        else
        {
            // This should never happen
            MXB_ERROR("Unexpected result state. cmd: 0x%02hhx, len: %u server: %s",
                      cmd, len, m_server.name());
            m_session->dump_statements();
            m_session->dump_session_log();
            m_session->kill();
            mxb_assert(!true);
        }
        break;

    case ReplyState::RSET_COLDEF:
        mxb_assert(m_num_coldefs > 0);
        --m_num_coldefs;

        if (m_num_coldefs == 0)
        {
            set_reply_state(use_deprecate_eof() ? ReplyState::RSET_ROWS : ReplyState::RSET_COLDEF_EOF);
        }
        break;

    case ReplyState::RSET_COLDEF_EOF:
        {
            mxb_assert(cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN);
            set_reply_state(ReplyState::RSET_ROWS);

            ++it;
            uint16_t warnings = mariadb::get_byte2(it);
            it += 2;

            m_reply.set_num_warnings(warnings);

            uint16_t status = mariadb::get_byte2(it);
            it += 2;

            m_reply.set_server_status(status);

            if (m_opening_cursor)
            {
                m_opening_cursor = false;

                // The cursor does not exist if the result contains only one row
                if (status & SERVER_STATUS_CURSOR_EXISTS)
                {
                    MXB_INFO("Cursor successfully opened");
                    set_reply_state(ReplyState::DONE);
                }
            }
        }
        break;

    case ReplyState::RSET_ROWS:
        if (cmd == MYSQL_REPLY_EOF && len == MYSQL_EOF_PACKET_LEN - MYSQL_HEADER_LEN)
        {
            // Genuine EOF packet
            ++it;
            uint16_t warnings = mariadb::get_byte2(it);
            it += 2;

            m_reply.set_num_warnings(warnings);

            uint16_t status = mariadb::get_byte2(it);
            it += 2;

            m_reply.set_server_status(status);
            bool more_results = (status & SERVER_MORE_RESULTS_EXIST);
            m_reply.set_multiresult(more_results);
            set_reply_state(more_results ? ReplyState::START : ReplyState::DONE);
        }
        else if (cmd == MYSQL_REPLY_EOF && len < 0xffffff - MYSQL_HEADER_LEN)
        {
            // OK packet pretending to be an EOF packet
            process_ok_packet(it, end);

            if (m_reply.state() != ReplyState::DONE)
            {
                // Resultset is complete but more data will follow
                set_reply_state(ReplyState::START);
            }
        }
        else if (cmd == MYSQL_REPLY_ERR)
        {
            ++it;
            update_error(it, end);
            set_reply_state(ReplyState::DONE);
        }
        else
        {
            m_reply.add_rows(1);

            if (m_collect_rows)
            {
                std::vector<std::string_view> row;

                for (uint64_t i = 0; i < m_reply.field_counts().back(); i++)
                {
                    row.push_back(get_encoded_str_sv(it));
                }

                m_reply.add_row_data(std::move(row));
            }
        }
        break;

    case ReplyState::PREPARE:
        if (use_deprecate_eof() || cmd == MYSQL_REPLY_EOF)
        {
            if (--m_ps_packets == 0)
            {
                set_reply_state(ReplyState::DONE);
            }
        }
        break;
    }
}

void MariaDBBackendConnection::process_ok_packet(Iter it, Iter end)
{
    ++it;                   // Skip the command byte
    skip_encoded_int(it);   // Affected rows
    skip_encoded_int(it);   // Last insert ID
    uint16_t status = mariadb::get_byte2(it);
    it += 2;

    m_reply.set_server_status(status);
    bool more_results = (status & SERVER_MORE_RESULTS_EXIST);
    m_reply.set_multiresult(more_results);

    if (!more_results)
    {
        // No more results
        set_reply_state(ReplyState::DONE);
    }

    // Two bytes of warnings
    uint16_t warnings = mariadb::get_byte2(it);
    it += 2;
    m_reply.set_num_warnings(warnings);

    if (rcap_type_required(m_session->capabilities(), RCAP_TYPE_SESSION_STATE_TRACKING)
        && (status & SERVER_SESSION_STATE_CHANGED))
    {
        // TODO: Benchmark the extra cost of always processing the session tracking variables and see if it's
        // too much.
        mxb_assert(server_capabilities & GW_MYSQL_CAPABILITIES_SESSION_TRACK);

        skip_encoded_str(it);   // Skip human-readable info

        // Skip the total packet length, we don't need it since we know it implicitly via the end iterator
        MXB_AT_DEBUG(ptrdiff_t total_size = ) get_encoded_int(it);
        mxb_assert(total_size == std::distance(it, end));

        while (it != end)
        {
            uint64_t type = *it++;
            uint64_t total_size = get_encoded_int(it);

            switch (type)
            {
            case SESSION_TRACK_STATE_CHANGE:
                it += total_size;
                break;

            case SESSION_TRACK_SCHEMA:
                skip_encoded_str(it);   // Schema name
                break;

            case SESSION_TRACK_GTIDS:
                skip_encoded_int(it);   // Encoding specification
                m_reply.set_variable(MXS_LAST_GTID, get_encoded_str(it));
                break;

            case SESSION_TRACK_TRANSACTION_CHARACTERISTICS:
                m_reply.set_variable("trx_characteristics", get_encoded_str(it));
                break;

            case SESSION_TRACK_SYSTEM_VARIABLES:
                {
                    auto name = get_encoded_str(it);
                    auto value = get_encoded_str(it);
                    m_reply.set_variable(name, value);
                }
                break;

            case SESSION_TRACK_TRANSACTION_TYPE:
                m_reply.set_variable("trx_state", get_encoded_str(it));
                break;

            default:
                mxb_assert(!true);
                it += total_size;
                MXB_WARNING("Received unexpecting session track type: %lu", type);
                break;
            }
        }
    }
}

/**
 * Extract prepared statement response
 *
 *  Contents of a COM_STMT_PREPARE_OK packet:
 *
 * [0]     OK (1)            -- always 0x00
 * [1-4]   statement_id (4)  -- statement-id
 * [5-6]   num_columns (2)   -- number of columns
 * [7-8]   num_params (2)    -- number of parameters
 * [9]     filler (1)
 * [10-11] warning_count (2) -- number of warnings
 *
 * The OK packet is followed by the parameter definitions terminated by an EOF packet and the field
 * definitions terminated by an EOF packet. If the DEPRECATE_EOF capability is set, the EOF packets are not
 * sent (currently not supported).
 *
 * @param it  Start of the packet payload
 * @param end Past-the-end iterator of the payload
 */
void MariaDBBackendConnection::process_ps_response(Iter it, Iter end)
{
    mxb_assert(*it == MYSQL_REPLY_OK);
    ++it;

    // Extract the PS ID generated by the server and replace it with our own. This allows the client protocol
    // to always refer to the same prepared statement with the same ID.
    uint32_t internal_id = m_current_id;
    uint32_t stmt_id = 0;
    mxb_assert(internal_id != 0);

    // Modifying the ID here is convenient but it doesn't seem right as the iterators should be const
    // iterators. This could be fixed later if a more suitable place is found.
    stmt_id = mariadb::get_byte4(it);
    mariadb::set_byte4(it, internal_id);
    it += 4;

    auto& ps_map = m_ps_map[internal_id];
    ps_map.real_id = stmt_id;
    MXB_INFO("PS internal ID %u maps to external ID %u on server '%s'",
             internal_id, stmt_id, m_dcb->server()->name());

    // Columns
    uint16_t columns = mariadb::get_byte2(it);
    it += 2;

    // Parameters
    uint16_t params = mariadb::get_byte2(it);
    it += 2;

    ps_map.n_params = params;

    // Always set our internal ID as the PS ID
    m_reply.set_generated_id(internal_id);
    m_reply.set_param_count(params);

    m_ps_packets = 0;

    // NOTE: The binary protocol is broken as it allows the column and parameter counts to overflow. This
    // means we can't rely on them if there ever is a query that has a column or parameter count that exceeds
    // the capacity of the 16-bit unsigned integer use to store it. If the client uses the DEPRECATE_EOF
    // capability, we have to count the individual packets instead of relying on the EOF packets.

    if (columns)
    {
        if (use_deprecate_eof())
        {
            m_ps_packets += columns;
        }
        else
        {
            // Server will send the column definition packets followed by an EOF packet.
            ++m_ps_packets;
        }
    }

    if (params)
    {
        if (use_deprecate_eof())
        {
            m_ps_packets += params;
        }
        else
        {
            // Server will send the parameter definition packets followed by an EOF packet
            ++m_ps_packets;
        }
    }

    set_reply_state(m_ps_packets == 0 ? ReplyState::DONE : ReplyState::PREPARE);
}

void MariaDBBackendConnection::process_reply_start(Iter it, Iter end)
{
    if (mxs_mysql_is_binlog_dump(m_reply.command()))
    {
        // Treat a binlog dump like a response that never ends
    }
    else if (m_reply.command() == MXS_COM_STATISTICS)
    {
        // COM_STATISTICS returns a single string and thus requires special handling:
        // https://mariadb.com/kb/en/library/com_statistics/#response
        set_reply_state(ReplyState::DONE);
    }
    else if (m_reply.command() == MXS_COM_FIELD_LIST && *it != MYSQL_REPLY_ERR)
    {
        // COM_FIELD_LIST sends a strange kind of a result set that doesn't have field definitions
        set_reply_state(ReplyState::RSET_ROWS);
    }
    else
    {
        process_result_start(it, end);
    }
}

void MariaDBBackendConnection::process_result_start(Iter it, Iter end)
{
    uint8_t cmd = *it;

    switch (cmd)
    {
    case MYSQL_REPLY_OK:
        m_reply.set_is_ok(true);

        if (m_reply.command() == MXS_COM_STMT_PREPARE)
        {
            process_ps_response(it, end);
        }
        else
        {
            process_ok_packet(it, end);
        }
        break;

    case MYSQL_REPLY_LOCAL_INFILE:
        // The client will send a request after this with the contents of the file which the server will
        // respond to with either an OK or an ERR packet
        set_reply_state(ReplyState::LOAD_DATA);
        break;

    case MYSQL_REPLY_ERR:
        // Nothing ever follows an error packet
        ++it;
        update_error(it, end);
        set_reply_state(ReplyState::DONE);
        break;

    case MYSQL_REPLY_EOF:
        // EOF packets are never expected as the first response unless changing user. For some reason the
        // server also responds with a EOF packet to COM_SET_OPTION even though, according to documentation,
        // it should respond with an OK packet.
        if (m_reply.command() == MXS_COM_SET_OPTION)
        {
            set_reply_state(ReplyState::DONE);
        }
        else
        {
            mxb_assert_message(!true, "Unexpected EOF packet");
        }
        break;

    default:
        // Start of a result set
        m_num_coldefs = get_encoded_int(it);
        m_reply.add_field_count(m_num_coldefs);

        if ((mysql_session()->extra_capabilities() & MXS_MARIA_CAP_CACHE_METADATA) && *it == 0)
        {
            set_reply_state(use_deprecate_eof() ? ReplyState::RSET_ROWS : ReplyState::RSET_COLDEF_EOF);
        }
        else
        {
            set_reply_state(ReplyState::RSET_COLDEF);
        }
        break;
    }
}

/**
 * Update @c m_error.
 *
 * @param it   Iterator that points to the first byte of the error code in an error packet.
 * @param end  Iterator pointing one past the end of the error packet.
 */
void MariaDBBackendConnection::update_error(Iter it, Iter end)
{
    uint16_t code = mariadb::get_byte2(it);
    it += 2;

    ++it;
    auto sql_state_begin = it;
    it += 5;
    auto sql_state_end = it;
    auto message_begin = sql_state_end;
    auto message_end = end;

    m_reply.set_error(code, sql_state_begin, sql_state_end, message_begin, message_end);
}

uint64_t MariaDBBackendConnection::thread_id() const
{
    return m_thread_id;
}

void MariaDBBackendConnection::assign_session(MXS_SESSION* session, mxs::Component* upstream)
{
    m_session = session;
    m_upstream = upstream;
    MYSQL_session* client_data = mysql_session();
    m_auth_data.client_data = client_data;
    m_authenticator = client_data->auth_data->be_auth_module->create_backend_authenticator(m_auth_data);
}

MariaDBBackendConnection::TrackedQuery::TrackedQuery(const GWBUF& buffer)
    : payload_len(MYSQL_GET_PAYLOAD_LEN(buffer.data()))
    , command(MYSQL_GET_COMMAND(buffer.data()))
    , collect_rows(buffer.type_is_collect_rows())
    , id(buffer.id())
{
    if (command == MXS_COM_STMT_EXECUTE)
    {
        // Extract the flag byte after the statement ID
        uint8_t flags = buffer[MYSQL_PS_ID_OFFSET + MYSQL_PS_ID_SIZE];

        // Any non-zero flag value means that we have an open cursor
        opening_cursor = flags != 0;
    }
}

/**
 * Track a client query
 *
 * Inspects the query and tracks the current command being executed. Also handles detection of
 * multi-packet requests and the special handling that various commands need.
 */
void MariaDBBackendConnection::track_query(const TrackedQuery& query)
{
    mxb_assert(m_state == State::ROUTING || m_state == State::SEND_HISTORY
               || m_state == State::READ_HISTORY || m_state == State::PREPARE_PS
               || m_state == State::SEND_CHANGE_USER || m_state == State::RESET_CONNECTION_FAST);

    m_reply.clear();
    m_reply.set_command(query.command);

    // Track the ID that the client protocol assigned to this query. It is used to verify that the result
    // from this backend matches the one that was sent upstream.
    m_current_id = query.id;

    m_collect_rows = query.collect_rows;

    if (mxs_mysql_command_will_respond(m_reply.command()))
    {
        set_reply_state(ReplyState::START);
    }

    if (m_reply.command() == MXS_COM_STMT_EXECUTE)
    {
        m_opening_cursor = query.opening_cursor;
    }
    else if (m_reply.command() == MXS_COM_STMT_FETCH)
    {
        set_reply_state(ReplyState::RSET_ROWS);
    }
}

MariaDBBackendConnection::~MariaDBBackendConnection()
{
}

void MariaDBBackendConnection::set_dcb(DCB* dcb)
{
    m_dcb = static_cast<BackendDCB*>(dcb);
}

const BackendDCB* MariaDBBackendConnection::dcb() const
{
    return m_dcb;
}

BackendDCB* MariaDBBackendConnection::dcb()
{
    return m_dcb;
}

void MariaDBBackendConnection::set_reply_state(mxs::ReplyState state)
{
    m_reply.set_reply_state(state);
}

std::string MariaDBBackendConnection::to_string(State auth_state)
{
    std::string rval;
    switch (auth_state)
    {
    case State::HANDSHAKING:
        rval = "Handshaking";
        break;

    case State::AUTHENTICATING:
        rval = "Authenticating";
        break;

    case State::CONNECTION_INIT:
        rval = "Sending connection initialization queries";
        break;

    case State::SEND_DELAYQ:
        rval = "Sending delayed queries";
        break;

    case State::FAILED:
        rval = "Failed";
        break;

    case State::ROUTING:
        rval = "Routing";
        break;

    case State::RESET_CONNECTION:
        rval = "Resetting connection";
        break;

    case State::RESET_CONNECTION_FAST:
        rval = "Fast connection reset";
        break;

    case State::READ_CHANGE_USER:
        rval = "Reading change user response";
        break;

    case State::SEND_CHANGE_USER:
        rval = "Sending change user";
        break;

    case State::PINGING:
        rval = "Pinging server";
        break;

    case State::POOLED:
        rval = "In pool";
        break;

    case State::SEND_HISTORY:
        rval = "Sending stored session command history";
        break;

    case State::READ_HISTORY:
        rval = "Reading results of history execution";
        break;

    case State::PREPARE_PS:
        rval = "Preparing a prepared statement";
        break;
    }
    return rval;
}

MariaDBBackendConnection::StateMachineRes MariaDBBackendConnection::handshake()
{
    auto rval = StateMachineRes::ERROR;
    bool state_machine_continue = true;

    while (state_machine_continue)
    {
        switch (m_hs_state)
        {
        case HandShakeState::SEND_PROHY_HDR:
            if (m_server.proxy_protocol())
            {
                // If read was the first event triggered, send proxy header.
                m_hs_state = (send_proxy_protocol_header()) ? HandShakeState::EXPECT_HS :
                    HandShakeState::FAIL;
            }
            else
            {
                m_hs_state = HandShakeState::EXPECT_HS;
            }
            break;

        case HandShakeState::EXPECT_HS:
            {
                // Read the server handshake.
                auto [read_ok, buffer] = mariadb::read_protocol_packet(m_dcb);
                if (buffer.empty())
                {
                    if (read_ok)
                    {
                        // Only got a partial packet, wait for more.
                        state_machine_continue = false;
                        rval = StateMachineRes::IN_PROGRESS;
                    }
                    else
                    {
                        // Socket error.
                        string errmsg = (string)"Handshake with '" + m_server.name() + "' failed.";
                        do_handle_error(m_dcb, errmsg, mxs::ErrorType::TRANSIENT);
                        m_hs_state = HandShakeState::FAIL;
                    }
                }
                else if (mxs_mysql_get_command(&buffer) == MYSQL_REPLY_ERR)
                {
                    // Server responded with an error instead of a handshake, probably too many connections.
                    do_handle_error(m_dcb, "Connection rejected: " + mxs::extract_error(&buffer),
                                    mxs::ErrorType::TRANSIENT);
                    m_hs_state = HandShakeState::FAIL;
                }
                else
                {
                    // Have a complete response from the server.
                    if (read_backend_handshake(std::move(buffer)))
                    {
                        if (capability_mismatch())
                        {
                            do_handle_error(m_dcb, "Capability mismatch", mxs::ErrorType::PERMANENT);
                            m_hs_state = HandShakeState::FAIL;
                        }
                        else
                        {
                            m_hs_state = m_dcb->using_ssl() ? HandShakeState::START_SSL :
                                HandShakeState::SEND_HS_RESP;
                        }
                    }
                    else
                    {
                        do_handle_error(m_dcb, "Bad handshake", mxs::ErrorType::TRANSIENT);
                        m_hs_state = HandShakeState::FAIL;
                    }
                }
            }
            break;

        case HandShakeState::START_SSL:
            {
                // SSL-connection starts by sending a cleartext SSLRequest-packet,
                // then initiating SSL-negotiation.
                GWBUF ssl_req = gw_generate_auth_response(true, false, m_dcb->service()->capabilities());
                if (m_dcb->writeq_append(std::move(ssl_req)) && m_dcb->ssl_handshake() >= 0)
                {
                    m_hs_state = HandShakeState::SSL_NEG;
                }
                else
                {
                    do_handle_error(m_dcb, "SSL failed", mxs::ErrorType::TRANSIENT);
                    m_hs_state = HandShakeState::FAIL;
                }
            }
            break;

        case HandShakeState::SSL_NEG:
            {
                // Check SSL-state.
                auto ssl_state = m_dcb->ssl_state();
                if (ssl_state == DCB::SSLState::ESTABLISHED)
                {
                    m_hs_state = HandShakeState::SEND_HS_RESP;      // SSL ready
                }
                else if (ssl_state == DCB::SSLState::HANDSHAKE_REQUIRED)
                {
                    state_machine_continue = false;     // in progress, wait for more data
                    rval = StateMachineRes::IN_PROGRESS;
                }
                else
                {
                    do_handle_error(m_dcb, "SSL failed", mxs::ErrorType::TRANSIENT);
                    m_hs_state = HandShakeState::FAIL;
                }
            }
            break;

        case HandShakeState::SEND_HS_RESP:
            {
                bool with_ssl = m_dcb->using_ssl();
                GWBUF hs_resp = gw_generate_auth_response(with_ssl, with_ssl,
                                                          m_dcb->service()->capabilities());
                if (m_dcb->writeq_append(std::move(hs_resp)))
                {
                    m_hs_state = HandShakeState::COMPLETE;
                }
                else
                {
                    m_hs_state = HandShakeState::FAIL;
                }
            }
            break;

        case HandShakeState::COMPLETE:
            state_machine_continue = false;
            rval = StateMachineRes::DONE;
            break;

        case HandShakeState::FAIL:
            state_machine_continue = false;
            rval = StateMachineRes::ERROR;
            break;
        }
    }
    return rval;
}

MariaDBBackendConnection::StateMachineRes MariaDBBackendConnection::authenticate()
{
    auto [read_ok, buffer] = mariadb::read_protocol_packet(m_dcb);
    if (buffer.empty())
    {
        if (read_ok)
        {
            // Didn't get enough data, read again later.
            return StateMachineRes::IN_PROGRESS;
        }
        else
        {
            do_handle_error(m_dcb, "Socket error", mxs::ErrorType::TRANSIENT);
            return StateMachineRes::ERROR;
        }
    }
    else if (buffer.length() == MYSQL_HEADER_LEN)
    {
        // Effectively empty buffer. Should not happen during authentication. Error.
        do_handle_error(m_dcb, "Invalid packet", mxs::ErrorType::TRANSIENT);
        return StateMachineRes::ERROR;
    }

    // Have a complete response from the server.
    uint8_t cmd = MYSQL_GET_COMMAND(buffer.data());

    // Three options: OK, ERROR or AuthSwitch/other.
    auto rval = StateMachineRes::ERROR;
    if (cmd == MYSQL_REPLY_OK)
    {
        MXB_INFO("Authentication to '%s' succeeded.", m_server.name());
        rval = StateMachineRes::DONE;
    }
    else if (cmd == MYSQL_REPLY_ERR)
    {
        // Server responded with an error, authentication failed.
        handle_error_response(m_dcb, &buffer);
        rval = StateMachineRes::ERROR;
    }
    else
    {
        // Something else, likely AuthSwitch or a message to the authentication plugin.
        auto res = m_authenticator->exchange(move(buffer));
        if (!res.output.empty())
        {
            m_dcb->writeq_append(move(res.output));
        }

        rval = res.success ? StateMachineRes::IN_PROGRESS : StateMachineRes::ERROR;
    }

    return rval;
}

bool MariaDBBackendConnection::send_delayed_packets()
{
    bool rval = true;

    // Store the packets in a local variable to prevent modifications to m_delayed_packets while we're
    // iterating it. This can happen if one of the packets causes the state to change from State::ROUTING to
    // something else (e.g. multiple COM_STMT_PREPARE packets being sent at the same time).
    auto packets = std::move(m_delayed_packets);

    for (auto it = packets.begin(); it != packets.end(); ++it)
    {
        if (!write(std::move(*it)))
        {
            rval = false;
            break;
        }
        else if (m_state != State::ROUTING)
        {
            // One of the packets caused the state to change. Put the rest of the packets back into the
            // delayed packet queue.
            mxb_assert(m_delayed_packets.empty());
            m_delayed_packets.assign(std::next(it), packets.end());
            break;
        }
    }

    return rval;
}

MariaDBBackendConnection::StateMachineRes MariaDBBackendConnection::send_connection_init_queries()
{
    auto rval = StateMachineRes::ERROR;
    switch (m_init_query_status.state)
    {
    case InitQueryStatus::State::SENDING:
        {
            // First time in this function.
            const auto& init_query_data = m_session->listener_data()->m_conn_init_sql;
            const auto& query_contents = init_query_data.buffer_contents;
            if (query_contents.empty())
            {
                rval = StateMachineRes::DONE;   // no init queries configured, continue normally
            }
            else
            {
                // Send all the initialization queries in one packet. The server should respond with one
                // OK-packet per query.
                m_dcb->writeq_append(GWBUF(query_contents));
                m_init_query_status.ok_packets_expected = init_query_data.queries.size();
                m_init_query_status.ok_packets_received = 0;
                m_init_query_status.state = InitQueryStatus::State::RECEIVING;
                rval = StateMachineRes::IN_PROGRESS;
            }
        }
        break;

    case InitQueryStatus::State::RECEIVING:
        while (m_init_query_status.ok_packets_received < m_init_query_status.ok_packets_expected)
        {
            // Check result. If server returned anything else than OK, it's an error.
            auto [read_ok, buffer] = mariadb::read_protocol_packet(m_dcb);
            if (buffer.empty())
            {
                if (read_ok)
                {
                    // Didn't get enough data, read again later.
                    rval = StateMachineRes::IN_PROGRESS;
                }
                else
                {
                    do_handle_error(m_dcb, "Socket error", mxs::ErrorType::TRANSIENT);
                }

                break;
            }
            else
            {
                string wrong_packet_type;
                if (buffer.length() == MYSQL_HEADER_LEN)
                {
                    wrong_packet_type = "an empty packet";
                }
                else
                {
                    uint8_t cmd = MYSQL_GET_COMMAND(buffer.data());
                    if (cmd == MYSQL_REPLY_ERR)
                    {
                        wrong_packet_type = "an error packet";
                    }
                    else if (cmd != MYSQL_REPLY_OK)
                    {
                        wrong_packet_type = "a resultset packet";
                    }
                }

                if (wrong_packet_type.empty())
                {
                    // Got an ok packet.
                    m_init_query_status.ok_packets_received++;
                }
                else
                {
                    // Query failed or gave weird results.
                    const auto& init_queries = m_session->listener_data()->m_conn_init_sql.queries;
                    const string& errored_query = init_queries[m_init_query_status.ok_packets_received];
                    string errmsg = mxb::string_printf("Connection initialization query '%s' returned %s.",
                                                       errored_query.c_str(), wrong_packet_type.c_str());
                    do_handle_error(m_dcb, errmsg, mxs::ErrorType::PERMANENT);
                    break;
                }
            }
        }

        if (m_init_query_status.ok_packets_received == m_init_query_status.ok_packets_expected)
        {
            rval = StateMachineRes::DONE;
        }
        break;
    }
    return rval;
}

void MariaDBBackendConnection::set_to_pooled()
{
    auto* ms = mysql_session();
    m_capabilities = ms->full_capabilities();
    m_account = m_session->user_and_host();
    m_db = ms->current_db;
    ms->history_info.erase(this);

    m_session = nullptr;
    m_upstream = nullptr;
    m_state = State::POOLED;
    // TODO: Likely other fields need to be modified as well, either here or in 'reuse_connection'.
    // Clean it up once situation clarifies.
}

mxs::Component* MariaDBBackendConnection::upstream() const
{
    return m_upstream;
}

bool MariaDBBackendConnection::expecting_reply() const
{
    return !m_reply.is_complete() || !m_track_queue.empty();
}

const MariaDBUserCache* MariaDBBackendConnection::user_account_cache()
{
    auto users = m_session->service->user_account_cache();
    // MariaDBBackendConnections may be used by other protocols than just MariaDB. The user account cache
    // may not exist or may be a different class. For now, only update it when using MariaDB-protocol.
    return dynamic_cast<const MariaDBUserCache*>(users);
}
