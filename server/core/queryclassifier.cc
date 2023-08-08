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

#include <maxscale/queryclassifier.hh>
#include <unordered_map>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>
#include <maxsimd/multistmt.hh>

using mariadb::QueryClassifier;
using mxs::Parser;

namespace
{

const int QC_TRACE_MSG_LEN = 1000;

struct DummyHandler final : public mariadb::QueryClassifier::Handler
{
    bool lock_to_master() override
    {
        return true;
    }

    bool is_locked_to_master() const override
    {
        return false;
    }

    bool supports_hint(Hint::Type hint_type) const override
    {
        return false;
    }
};

static DummyHandler dummy_handler;

std::string get_current_db(MXS_SESSION* session)
{
    return session->client_connection()->current_db();
}

uint32_t get_prepare_type(const mxs::Parser& parser, const GWBUF& buffer)
{
    uint32_t type = mxs::sql::TYPE_UNKNOWN;

    if (parser.is_prepare(buffer))
    {
        type = parser.get_type_mask(buffer) & ~mxs::sql::TYPE_PREPARE_STMT;
    }
    else
    {
        GWBUF* stmt = parser.get_preparable_stmt(buffer);

        if (stmt)
        {
            type = parser.get_type_mask(*stmt);
        }
    }

    return type;
}

std::string get_text_ps_id(const mxs::Parser& parser, const GWBUF& buffer)
{
    return std::string(parser.get_prepare_name(buffer));
}

bool foreach_table(QueryClassifier& qc,
                   MXS_SESSION* pSession,
                   GWBUF* querybuf,
                   bool (* func)(QueryClassifier& qc, const std::string&))
{
    bool rval = true;

    for (const auto& t : qc.parser().get_table_names(*querybuf))
    {
        std::string table;

        if (t.db.empty())
        {
            table = get_current_db(pSession);
        }
        else
        {
            table = t.db;
        }

        table += ".";
        table += t.table;

        if (!func(qc, table))
        {
            rval = false;
            break;
        }
    }

    return rval;
}
}

namespace mariadb
{

class QueryClassifier::PSManager
{
    PSManager(const PSManager&) = delete;
    PSManager& operator=(const PSManager&) = delete;

public:
    struct PreparedStmt
    {
        uint32_t type = 0;
        uint16_t param_count = 0;
        bool     route_to_last_used = false;
    };

    PSManager(mxs::Parser& parser, Log log)
        : m_parser(parser)
        , m_log(log)
    {
    }

    ~PSManager()
    {
    }

    void store(GWBUF* buffer, uint32_t id)
    {
        bool is_prepare = m_parser.is_prepare(*buffer);

        mxb_assert(is_prepare
                   || Parser::type_mask_contains(m_parser.get_type_mask(*buffer),
                                                 mxs::sql::TYPE_PREPARE_NAMED_STMT));

        PreparedStmt stmt;
        stmt.type = get_prepare_type(m_parser, *buffer);
        stmt.route_to_last_used = m_parser.relates_to_previous(*buffer);

        if (is_prepare)
        {
            m_binary_ps.emplace(id, std::move(stmt));
        }
        else if (m_parser.is_query(*buffer))
        {
            m_text_ps.emplace(get_text_ps_id(m_parser, *buffer), std::move(stmt));
        }
        else
        {
            mxb_assert(!true);
        }
    }

    const PreparedStmt* get(uint32_t id) const
    {
        const PreparedStmt* rval = nullptr;
        BinaryPSMap::const_iterator it = m_binary_ps.find(id);

        if (it != m_binary_ps.end())
        {
            rval = &it->second;
        }
        else if (m_parser.is_execute_immediately_ps(id))
        {
            if (m_log == Log::ALL)
            {
                MXB_WARNING("Using unknown prepared statement with ID %u", id);
            }
        }

        return rval;
    }

    const PreparedStmt* get(std::string id) const
    {
        const PreparedStmt* rval = nullptr;
        TextPSMap::const_iterator it = m_text_ps.find(id);

        if (it != m_text_ps.end())
        {
            rval = &it->second;
        }
        else if (m_log == Log::ALL)
        {
            MXB_WARNING("Using unknown prepared statement with ID '%s'", id.c_str());
        }

        return rval;
    }

    void erase(std::string id)
    {
        if (m_text_ps.erase(id) == 0)
        {
            if (m_log == Log::ALL)
            {
                MXB_WARNING("Closing unknown prepared statement with ID '%s'", id.c_str());
            }
        }
    }

    void erase(uint32_t id)
    {
        if (m_binary_ps.erase(id) == 0)
        {
            if (m_log == Log::ALL)
            {
                MXB_WARNING("Closing unknown prepared statement with ID %u", id);
            }
        }
    }

    void erase(GWBUF* buffer)
    {
        if (m_parser.is_query(*buffer))
        {
            erase(get_text_ps_id(m_parser, *buffer));
        }
        else if (m_parser.is_ps_packet(*buffer))
        {
            erase(m_parser.get_ps_id(*buffer));
        }
        else
        {
            mxb_assert_message(!true, "QueryClassifier::PSManager::erase called with invalid query");
        }
    }

    void set_param_count(uint32_t id, uint16_t param_count)
    {
        m_binary_ps[id].param_count = param_count;
    }

    uint16_t param_count(uint32_t id) const
    {
        uint16_t rval = 0;
        auto it = m_binary_ps.find(id);

        if (it != m_binary_ps.end())
        {
            rval = it->second.param_count;
        }

        return rval;
    }

private:

    using BinaryPSMap = std::unordered_map<uint32_t, PreparedStmt>;
    using TextPSMap = std::unordered_map<std::string, PreparedStmt>;

private:
    mxs::Parser& m_parser;
    BinaryPSMap  m_binary_ps;
    TextPSMap    m_text_ps;
    Log          m_log;
};

//
// QueryClassifier
//

QueryClassifier::QueryClassifier(mxs::Parser& parser, MXS_SESSION* pSession)
    : QueryClassifier(parser, &dummy_handler, pSession, TYPE_ALL, Log::NONE)
{
    m_verbose = false;
}

QueryClassifier::QueryClassifier(mxs::Parser& parser,
                                 Handler* pHandler,
                                 MXS_SESSION* pSession,
                                 mxs_target_t use_sql_variables_in,
                                 Log log)
    : m_parser(parser)
    , m_pHandler(pHandler)
    , m_pSession(pSession)
    , m_use_sql_variables_in(use_sql_variables_in)
    , m_multi_statements_allowed(pSession->protocol_data()->are_multi_statements_allowed())
    , m_sPs_manager(new PSManager(parser, log))
    , m_route_info(&parser)
    , m_prev_route_info(&parser)
{
}

void QueryClassifier::ps_store(GWBUF* pBuffer, uint32_t id)
{
    m_prev_ps_id = id;
    return m_sPs_manager->store(pBuffer, id);
}

void QueryClassifier::ps_erase(GWBUF* buffer)
{
    if (m_parser.is_ps_packet(*buffer))
    {
        // Erase the type of the statement stored with the internal ID
        m_sPs_manager->erase(ps_id_internal_get(buffer));
    }
    else
    {
        // Not a PS command, we don't need the ID mapping
        m_sPs_manager->erase(buffer);
    }
}

bool QueryClassifier::query_type_is_read_only(uint32_t qtype) const
{
    bool rval = false;

    if (!Parser::type_mask_contains(qtype, mxs::sql::TYPE_MASTER_READ)
        && !Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
        && (Parser::type_mask_contains(qtype, mxs::sql::TYPE_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ)))
    {
        if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ))
        {
            if (m_use_sql_variables_in == TYPE_ALL)
            {
                rval = true;
            }
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

void QueryClassifier::process_routing_hints(const GWBUF::HintVector& hints, uint32_t* target)
{
    using Type = Hint::Type;
    const char max_rlag[] = "max_slave_replication_lag";

    bool check_more = true;
    for (auto it = hints.begin(); check_more && it != hints.end(); it++)
    {
        const Hint& hint = *it;
        if (m_pHandler->supports_hint(hint.type))
        {
            switch (hint.type)
            {
            case Type::ROUTE_TO_MASTER:
                // This means override, so we bail out immediately.
                *target = TARGET_MASTER;
                MXB_DEBUG("Hint: route to primary");
                check_more = false;
                break;

            case Type::ROUTE_TO_NAMED_SERVER:
                // The router is expected to look up the named server.
                *target |= TARGET_NAMED_SERVER;
                MXB_DEBUG("Hint: route to named server: %s", hint.data.c_str());
                break;

            case Type::ROUTE_TO_UPTODATE_SERVER:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_ALL:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_LAST_USED:
                MXB_DEBUG("Hint: route to last used");
                *target = TARGET_LAST_USED;
                break;

            case Type::PARAMETER:
                if (strncasecmp(hint.data.c_str(), max_rlag, sizeof(max_rlag) - 1) == 0)
                {
                    *target |= TARGET_RLAG_MAX;
                }
                else
                {
                    MXB_ERROR("Unknown hint parameter '%s' when '%s' was expected.",
                              hint.data.c_str(), max_rlag);
                }
                break;

            case Type::ROUTE_TO_SLAVE:
                *target = TARGET_SLAVE;
                MXB_DEBUG("Hint: route to replica.");
                break;

            case Type::NONE:
                mxb_assert(!true);
                break;
            }
        }
    }
}

uint32_t QueryClassifier::get_route_target(uint32_t qtype, const TrxTracker& trx_tracker)
{
    bool trx_active = trx_tracker.is_trx_active();
    uint32_t target = TARGET_UNDEFINED;
    bool load_active = (m_route_info.load_data_state() != LOAD_DATA_INACTIVE);
    mxb_assert(!load_active);

    /**
     * Prepared statements preparations should go to all servers
     */
    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_STMT)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_NAMED_STMT))
    {
        target = TARGET_ALL;
    }
    /**
     * These queries should be routed to all servers
     */
    else if (!load_active && !Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
             && (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SESSION_WRITE)
                 ||     /** Configured to allow writing user variables to all nodes */
                 (m_use_sql_variables_in == TYPE_ALL
                  && Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_WRITE))
                 || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_WRITE)
                 ||     /** enable or disable autocommit are always routed to all */
                 Parser::type_mask_contains(qtype, mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
                 || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DISABLE_AUTOCOMMIT)))
    {
        target |= TARGET_ALL;
    }
    /**
     * Hints may affect on routing of the following queries
     */
    else if (!trx_active && !load_active && query_type_is_read_only(qtype))
    {
        target = TARGET_SLAVE;
    }
    else if (trx_tracker.is_trx_read_only())
    {
        /* Force TARGET_SLAVE for READ ONLY transaction (active or ending) */
        target = TARGET_SLAVE;
    }
    else
    {
        mxb_assert(trx_active || load_active
                   || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_MASTER_READ)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SESSION_WRITE)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_BEGIN_TRX)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DISABLE_AUTOCOMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_ROLLBACK)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_COMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_EXEC_STMT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_CREATE_TMP_TABLE)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_UNKNOWN))
                   || Parser::type_mask_contains(qtype, mxs::sql::TYPE_EXEC_STMT));

        target = TARGET_MASTER;
    }

    return target;
}

uint32_t QueryClassifier::ps_id_internal_get(GWBUF* pBuffer)
{
    uint32_t id = m_parser.get_ps_id(*pBuffer);

    // Do we implicitly refer to the previous prepared statement.
    if (m_parser.is_ps_direct_exec_id(id) && m_prev_ps_id)
    {
        return m_prev_ps_id;
    }

    return id;
}

void QueryClassifier::ps_store_response(uint32_t id, uint16_t param_count)
{
    // The previous PS ID can be larger than the ID of the response being stored if multiple prepared
    // statements were sent at the same time.
    mxb_assert(m_prev_ps_id == id);

    if (param_count)
    {
        m_sPs_manager->set_param_count(id, param_count);
    }
}

void QueryClassifier::log_transaction_status(GWBUF* querybuf, uint32_t qtype, const TrxTracker& trx_tracker)
{
    if (m_route_info.multi_part_packet())
    {
        MXB_INFO("> Processing large request with more than 2^24 bytes of data");
    }
    else if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_INACTIVE)
    {
        MXB_INFO("> Autocommit: %s, trx is %s, %s",
                 trx_tracker.is_autocommit() ? "[enabled]" : "[disabled]",
                 trx_tracker.is_trx_active() ? "[open]" : "[not open]",
                 session()->protocol()->describe(*querybuf).c_str());
    }
    else
    {
        MXB_INFO("> Processing LOAD DATA LOCAL INFILE.");
    }
}

uint32_t QueryClassifier::determine_query_type(const GWBUF& packet) const
{
    uint32_t type_mask = mxs::sql::TYPE_UNKNOWN;

    mxs::Parser::PacketTypeMask ptm = m_parser.get_packet_type_mask(packet);

    if (ptm.second == mxs::Parser::TypeMaskStatus::FINAL)
    {
        type_mask = ptm.first;
    }
    else
    {
        type_mask = m_parser.get_type_mask(packet);
    }

    return type_mask;
}

void QueryClassifier::check_create_tmp_table(GWBUF* querybuf, uint32_t type)
{
    if (Parser::type_mask_contains(type, mxs::sql::TYPE_CREATE_TMP_TABLE))
    {
        std::string table;

        for (const auto& t : m_parser.get_table_names(*querybuf))
        {
            if (t.db.empty())
            {
                table = get_current_db(session());
            }
            else
            {
                table = t.db;
            }

            table += '.';
            table += t.table;
            break;
        }

        MXB_INFO("Added temporary table %s", table.c_str());

        /** Add the table to the set of temporary tables */
        m_route_info.add_tmp_table(table);
    }
}

bool QueryClassifier::is_read_tmp_table(GWBUF* querybuf, uint32_t qtype)
{
    bool rval = false;

    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ))
    {
        if (!foreach_table(*this, m_pSession, querybuf, &QueryClassifier::find_table))
        {
            rval = true;
        }
    }

    return rval;
}

void QueryClassifier::check_drop_tmp_table(GWBUF* querybuf)
{
    if (m_parser.get_operation(*querybuf) == mxs::sql::OP_DROP_TABLE)
    {
        foreach_table(*this, m_pSession, querybuf, &QueryClassifier::delete_table);
    }
}

/**
 * @brief Handle multi statement queries and load statements
 *
 * One of the possible types of handling required when a request is routed
 *
 * @param qc                   The query classifier
 * @param current_target       The current target
 * @param querybuf             Buffer containing query to be routed
 * @param qtype                Query type
 *
 * @return QueryClassifier::CURRENT_TARGET_MASTER if the session should be fixed
 *         to the master, QueryClassifier::CURRENT_TARGET_UNDEFINED otherwise.
 */
QueryClassifier::current_target_t QueryClassifier::handle_multi_temp_and_load(
    QueryClassifier::current_target_t current_target,
    GWBUF* querybuf,
    uint32_t* qtype)
{
    QueryClassifier::current_target_t rv = QueryClassifier::CURRENT_TARGET_UNDEFINED;

    bool is_query = m_parser.is_query(*querybuf);

    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed. */
    if (current_target != QueryClassifier::CURRENT_TARGET_MASTER)
    {
        bool is_multi = is_query && m_parser.get_operation(*querybuf) == mxs::sql::OP_CALL;
        if (!is_multi && multi_statements_allowed() && is_query)
        {
            is_multi = maxsimd::is_multi_stmt(m_parser.helper().get_sql(*querybuf));
        }

        if (is_multi)
        {
            rv = QueryClassifier::CURRENT_TARGET_MASTER;
        }
    }

    /**
     * Check if the query has anything to do with temporary tables.
     */
    if (m_route_info.have_tmp_tables() && is_query)
    {

        check_drop_tmp_table(querybuf);
        if (is_read_tmp_table(querybuf, *qtype))
        {
            *qtype |= mxs::sql::TYPE_MASTER_READ;
        }
    }

    check_create_tmp_table(querybuf, *qtype);

    return rv;
}

uint16_t QueryClassifier::get_param_count(uint32_t id)
{
    return m_sPs_manager->param_count(id);
}

bool QueryClassifier::query_continues_ps(const GWBUF& buffer)
{
    return m_parser.continues_ps(buffer, m_route_info.command());
}

const QueryClassifier::RouteInfo& QueryClassifier::update_route_info(GWBUF& buffer)
{
    auto protocol_data = session()->protocol_data();
    TrxTracker tracker;
    tracker.m_autocommit = protocol_data->is_autocommit();

    if (protocol_data->is_trx_read_only())
    {
        tracker.m_trx_state |= TrxTracker::TrxState::TRX_READ_ONLY;
    }

    if (protocol_data->is_trx_ending())
    {
        tracker.m_trx_state |= TrxTracker::TrxState::TRX_ENDING;
    }

    if (protocol_data->is_trx_starting())
    {
        tracker.m_trx_state |= TrxTracker::TrxState::TRX_STARTING;
    }

    if (protocol_data->is_trx_active())
    {
        tracker.m_trx_state |= TrxTracker::TrxState::TRX_ACTIVE;
    }

    return update_route_info(buffer, tracker);
}

const QueryClassifier::RouteInfo&
QueryClassifier::update_route_info(GWBUF& buffer, const TrxTracker& trx_tracker)
{
    uint32_t route_target = TARGET_MASTER;
    uint8_t cmd = 0xFF;
    uint32_t type_mask = mxs::sql::TYPE_UNKNOWN;
    uint32_t stmt_id = 0;
    bool locked_to_master = m_pHandler->is_locked_to_master();
    current_target_t current_target = locked_to_master ? CURRENT_TARGET_MASTER : CURRENT_TARGET_UNDEFINED;

    // Stash the current state in case we need to roll it back
    m_prev_route_info = m_route_info;

    m_route_info.set_multi_part_packet(m_parser.is_multi_part_packet(buffer));

    if (m_route_info.multi_part_packet())
    {
        // Trailing part of a multi-packet query, ignore it
        return m_route_info;
    }

    // Reset for every classification
    m_route_info.set_ps_continuation(false);

    // TODO: It may be sufficient to simply check whether we are in a read-only
    // TODO: transaction.
    bool in_read_only_trx =
        (current_target != QueryClassifier::CURRENT_TARGET_UNDEFINED) && trx_tracker.is_trx_read_only();

    if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_ACTIVE)
    {
        // A LOAD DATA LOCAL INFILE is ongoing
    }
    else if (!m_parser.is_empty(buffer))
    {
        cmd = m_parser.get_command(buffer);

        if (m_parser.is_ps_packet(buffer))
        {
            stmt_id = ps_id_internal_get(&buffer);
        }

        /**
         * If the session is inside a read-only transaction, we trust that the
         * server acts properly even when non-read-only queries are executed.
         * For this reason, we can skip the parsing of the statement completely.
         */
        if (in_read_only_trx)
        {
            type_mask = mxs::sql::TYPE_READ;
        }
        else
        {
            type_mask = QueryClassifier::determine_query_type(buffer);

            current_target = handle_multi_temp_and_load(current_target,
                                                        &buffer,
                                                        &type_mask);

            if (current_target == QueryClassifier::CURRENT_TARGET_MASTER)
            {
                /* If we do not have a master node, assigning the forced node is not
                 * effective since we don't have a node to force queries to. In this
                 * situation, assigning mxs::sql::TYPE_WRITE for the query will trigger
                 * the error processing. */
                if (!m_pHandler->lock_to_master())
                {
                    type_mask |= mxs::sql::TYPE_WRITE;
                }
            }
        }

        /**
         * Find out where to route the query. Result may not be clear; it is
         * possible to have a hint for routing to a named server which can
         * be either slave or master.
         * If query would otherwise be routed to slave then the hint determines
         * actual target server if it exists.
         *
         * route_target is a bitfield and may include :
         * TARGET_ALL
         * - route to all connected backend servers
         * TARGET_SLAVE[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to hints, then to slave and if those
         *   failed, eventually to master
         * TARGET_MASTER[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to the hints and if they failed,
         *   eventually to master
         */

        bool route_to_last_used = false;

        if (locked_to_master)
        {
            /** The session is locked to the master */
            route_target = TARGET_MASTER;
        }
        else
        {
            bool is_query = m_parser.is_query(buffer);

            if (!in_read_only_trx
                && is_query
                && m_parser.get_operation(buffer) == mxs::sql::OP_EXECUTE)
            {
                if (const auto* ps = m_sPs_manager->get(get_text_ps_id(m_parser, buffer)))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                }
            }
            else if (m_parser.is_ps_packet(buffer))
            {
                if (const auto* ps = m_sPs_manager->get(stmt_id))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                    m_route_info.set_ps_continuation(query_continues_ps(buffer));
                }
            }
            else if (is_query && m_parser.relates_to_previous(buffer))
            {
                route_to_last_used = true;
            }

            route_target = get_route_target(type_mask, trx_tracker);

            if (route_target == TARGET_SLAVE && route_to_last_used)
            {
                route_target = TARGET_LAST_USED;
            }
        }

        process_routing_hints(buffer.hints(), &route_target);

        if (trx_tracker.is_trx_ending() || Parser::type_mask_contains(type_mask, mxs::sql::TYPE_BEGIN_TRX))
        {
            // Transaction is ending or starting
            m_route_info.set_trx_still_read_only(true);
        }
        else if (trx_tracker.is_trx_active() && !query_type_is_read_only(type_mask))
        {
            // Transaction is no longer read-only
            m_route_info.set_trx_still_read_only(false);
        }
    }

    if (m_verbose && mxb_log_should_log(LOG_INFO))
    {
        log_transaction_status(&buffer, type_mask, trx_tracker);
    }

    m_route_info.set_target(route_target);
    m_route_info.set_command(cmd);
    m_route_info.set_type_mask(type_mask);
    m_route_info.set_stmt_id(stmt_id);

    return m_route_info;
}

void QueryClassifier::update_from_reply(const mxs::Reply& reply)
{
    m_route_info.set_load_data_state(reply.state() == mxs::ReplyState::LOAD_DATA ?
                                     LOAD_DATA_ACTIVE : LOAD_DATA_INACTIVE);
}

// static
bool QueryClassifier::find_table(QueryClassifier& qc, const std::string& table)
{
    if (qc.m_route_info.is_tmp_table(table))
    {
        MXB_INFO("Query targets a temporary table: %s", table.c_str());
        return false;
    }

    return true;
}

// static
bool QueryClassifier::delete_table(QueryClassifier& qc, const std::string& table)
{
    qc.m_route_info.remove_tmp_table(table);
    return true;
}
}
