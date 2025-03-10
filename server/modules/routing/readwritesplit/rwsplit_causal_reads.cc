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

#include "rwsplitsession.hh"

/**
 * @bref discard the result of MASTER_GTID_WAIT statement
 *
 * The result will be an error or an OK packet.
 *
 * @param buffer Original reply buffer
 *
 * @return Any data after the ERR/OK packet, nullptr for no data
 */
GWBUF* RWSplitSession::discard_master_wait_gtid_result(GWBUF* buffer)
{
    uint8_t header_and_command[MYSQL_HEADER_LEN + 1];
    gwbuf_copy_data(buffer, 0, MYSQL_HEADER_LEN + 1, header_and_command);

    if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_OK)
    {
        // MASTER_WAIT_GTID is complete, discard the OK packet or return the ERR packet
        m_wait_gtid = UPDATING_PACKETS;

        // Discard the OK packet and start updating sequence numbers
        uint8_t packet_len = MYSQL_GET_PAYLOAD_LEN(header_and_command) + MYSQL_HEADER_LEN;
        m_next_seq = 1;
        buffer = gwbuf_consume(buffer, packet_len);
    }
    else if (MYSQL_GET_COMMAND(header_and_command) == MYSQL_REPLY_ERR)
    {
        if (trx_is_read_only())
        {
            // If a causal read fails inside of a read-only transaction, it cannot be retried on the master.
            m_wait_gtid = NONE;
            gwbuf_free(buffer);
            buffer = mariadb::create_error_packet_ptr(
                0, 1792, "25006",
                "Causal read timed out while in a read-only transaction, cannot retry command.");
        }
        else
        {
            // The MASTER_WAIT_GTID command failed and no further packets will come
            m_wait_gtid = RETRYING_ON_MASTER;
        }
    }

    return buffer;
}

/**
 * @bref After discarded the wait result, we need correct the seqence number of every packet
 *
 * @param buffer origin reply buffer
 *
 */
void RWSplitSession::correct_packet_sequence(GWBUF* buffer)
{
    auto it = buffer->begin();
    auto end = buffer->end();
    mxb_assert_message(buffer->length() > MYSQL_HEADER_LEN, "Should never receive partial packets");

    while (it < end)
    {
        mxb_assert(std::distance(it, end) > MYSQL_HEADER_LEN);
        uint32_t len = mariadb::get_byte3(it);
        it += 3;
        *it++ = m_next_seq++;

        // MXS-4172: If the buffer contains a partial packet, the `it < end` check will prevent it from going
        // past the end. This means that if a bug ends up returning either a partial packet or malformed data,
        // the iteration won't go past the end of the buffer.
        mxb_assert(std::distance(it, end) >= len);
        it += len;
    }
}

GWBUF* RWSplitSession::handle_causal_read_reply(GWBUF* writebuf,
                                                const mxs::Reply& reply,
                                                mxs::RWBackend* backend)
{
    if (m_config.causal_reads != CausalReads::NONE)
    {
        if (reply.is_ok() && backend == m_current_master)
        {
            auto gtid = reply.get_variable(MXS_LAST_GTID);

            if (!gtid.empty())
            {
                if (m_config.causal_reads == CausalReads::GLOBAL
                    || m_config.causal_reads == CausalReads::FAST_GLOBAL)
                {
                    m_router->set_last_gtid(gtid);
                }
                else
                {
                    m_gtid_pos = RWSplit::gtid::from_string(gtid);
                }
            }
        }

        if (m_wait_gtid == READING_GTID)
        {
            writebuf = parse_gtid_result(writebuf, reply);
        }

        if (m_wait_gtid == WAITING_FOR_HEADER)
        {
            mxb_assert(m_prev_plan.target == backend);
            writebuf = discard_master_wait_gtid_result(writebuf);
        }

        if (m_wait_gtid == UPDATING_PACKETS && writebuf)
        {
            mxb_assert(m_prev_plan.target == backend);
            correct_packet_sequence(writebuf);
        }
    }

    return writebuf;
}

bool RWSplitSession::should_do_causal_read() const
{
    switch (m_config.causal_reads)
    {
    case CausalReads::LOCAL:
        // Only do a causal read if we have a GTID to wait for
        return !m_gtid_pos.empty();

    case CausalReads::GLOBAL:
        return true;

    case CausalReads::UNIVERSAL:
        // The universal mode behaves like CausalReads::LOCAL after the GTID probe has completed.
        return m_wait_gtid == GTID_READ_DONE && !m_gtid_pos.empty();

    case CausalReads::FAST:
    case CausalReads::FAST_GLOBAL:
    case CausalReads::NONE:
        return false;

    default:
        mxb_assert(!true);
        return false;
    }
}

bool RWSplitSession::continue_causal_read()
{
    bool rval = false;

    if (m_wait_gtid == GTID_READ_DONE)
    {
        MXB_INFO("Continuing with causal read");
        mxb_assert(m_current_query.empty());
        mxb_assert(!m_query_queue.empty());

        retry_query(m_query_queue.front().release(), 0);
        m_query_queue.pop_front();
        rval = true;
    }
    else if (m_config.causal_reads != CausalReads::NONE && m_wait_gtid != GTID_READ_DONE)
    {
        if (m_wait_gtid == RETRYING_ON_MASTER)
        {
            // Retry the query on the master
            GWBUF* buf = m_current_query.release();
            buf->hints.emplace_back(Hint::Type::ROUTE_TO_MASTER);
            retry_query(buf, 0);
            rval = true;
        }

        // The reply should never be complete while we are still waiting for the header.
        mxb_assert(m_wait_gtid != WAITING_FOR_HEADER);
        m_wait_gtid = NONE;
    }

    return rval;
}

/*
 * Add a wait gitd query in front of user's query to achive causal read
 *
 * @param origin  Original buffer
 *
 * @return       A new buffer contains wait statement and origin query
 */
GWBUF* RWSplitSession::add_prefix_wait_gtid(GWBUF* origin)
{
    /**
     * Pack wait function and client query into a multistatments will save a round trip latency,
     * and prevent the client query being executed on timeout.
     * For example:
     * SET @maxscale_secret_variable=(SELECT CASE WHEN MASTER_GTID_WAIT('232-1-1', 10) = 0
     * THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END); SELECT * FROM `city`;
     * when MASTER_GTID_WAIT('232-1-1', 0.05) == 1 (timeout), it will return
     * an error, and SELECT * FROM `city` will not be executed, then we can retry
     * on master;
     **/

    uint64_t version = m_router->service()->get_version(SERVICE_VERSION_MIN);

    GWBUF* rval = origin;
    std::ostringstream ss;
    const char* wait_func = version > 50700 && version < 100000 ?
        "WAIT_FOR_EXECUTED_GTID_SET" : "MASTER_GTID_WAIT";
    std::string gtid_position = m_config.causal_reads == CausalReads::GLOBAL ?
        m_router->last_gtid() : m_gtid_pos.to_string();

    ss << "SET @maxscale_secret_variable=(SELECT CASE WHEN "
       << wait_func
       << "('" << gtid_position << "', " << m_config.causal_reads_timeout.count() << ") = 0 "
       << "THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END);";

    auto sql = ss.str();

    // Only do the replacement if it fits into one packet
    if (gwbuf_length(origin) + sql.size() < GW_MYSQL_MAX_PACKET_LEN + MYSQL_HEADER_LEN)
    {
        GWBUF* prefix_buff = modutil_create_query(sql.c_str());

        // Copy the original query in case it fails on the slave
        m_current_query.copy_from(origin);

        /* Trim origin to sql, Append origin buffer to the prefix buffer */
        uint8_t header[MYSQL_HEADER_LEN];
        gwbuf_copy_data(origin, 0, MYSQL_HEADER_LEN, header);
        /* Command length = 1 */
        size_t origin_sql_len = MYSQL_GET_PAYLOAD_LEN(header) - 1;
        /* Trim mysql header and command */
        origin = gwbuf_consume(origin, MYSQL_HEADER_LEN + 1);
        rval = gwbuf_append(prefix_buff, origin);

        /* Modify totol length: Prefix sql len + origin sql len + command len */
        size_t new_payload_len = sql.size() + origin_sql_len + 1;
        gw_mysql_set_byte3(GWBUF_DATA(rval), new_payload_len);

        m_wait_gtid = WAITING_FOR_HEADER;
    }

    return rval;
}

void RWSplitSession::send_sync_query(mxs::RWBackend* target)
{
    // Add a routing hint to the copy of the current query to prevent it from being routed to a slave if it
    // has to be retried.
    GWBUF* buf = m_current_query.release();
    buf->hints.emplace_back(Hint::Type::ROUTE_TO_MASTER);
    m_current_query.reset(buf);

    int64_t timeout = m_config.causal_reads_timeout.count();
    std::string gtid = m_config.causal_reads == CausalReads::GLOBAL ?
        m_router->last_gtid() : m_gtid_pos.to_string();

    // The following SQL will wait for the current GTID to be reached. If the GTID is not reached within
    // the given timeout, the connection will be closed. This will trigger the replaying of the current
    // statement which, due to the routing hint, will be retried on the current master. It will also abort the
    // execution of the query sent right after this one.
    std::ostringstream ss;
    ss << "IF (MASTER_GTID_WAIT('" << gtid << "', " << timeout << ") <> 0) THEN "
       << "KILL (SELECT CONNECTION_ID());"
       << "END IF";

    GWBUF* query = modutil_create_query(ss.str().c_str());
    target->write(query, mxs::Backend::IGNORE_RESPONSE);
}

std::pair<mxs::Buffer, RWSplitSession::RoutingPlan> RWSplitSession::start_gtid_probe()
{
    MXB_INFO("Starting GTID probe");

    m_wait_gtid = READING_GTID;
    mxs::Buffer buffer(modutil_create_query("SELECT @@gtid_current_pos"));
    buffer.add_hint(Hint::Type::ROUTE_TO_MASTER);
    buffer.set_type(GWBUF::TYPE_COLLECT_ROWS);

    m_qc.revert_update();
    m_qc.update_route_info(get_current_target(), buffer.get());
    RoutingPlan plan = resolve_route(buffer, route_info());

    // Now with MXS-4260 fixed, the attached routing hint will be more of a suggestion to the downstream
    // components rather than something that must be followed. For this reason, the target type must be
    // explicitly set as TARGET_MASTER. In addition, the actual target must be re-selected every time to make
    // sure that a new connection is created if the master changes and/or dies during a read-only transaction
    // that's being replayed.
    plan.route_target = TARGET_MASTER;
    plan.target = handle_master_is_target();

    return {buffer, plan};
}

mxs::Buffer RWSplitSession::reset_gtid_probe()
{
    mxb_assert_message(m_current_query.empty(), "Current query should be empty but it contains: %s",
                       m_current_query.get_sql().c_str());
    mxb_assert_message(!m_query_queue.empty(), "Query queue should contain at least one query");

    // Retry the the original query that triggered the GTID probe.
    auto buffer = std::move(m_query_queue.front());
    m_query_queue.pop_front();

    // Revert back to the default state. This causes the GTID probe to start again. If we cannot
    // reconnect to the master, the session will be closed when the next GTID probe is routed.
    m_wait_gtid = NONE;

    return buffer;
}

GWBUF* RWSplitSession::parse_gtid_result(GWBUF* buffer, const mxs::Reply& reply)
{
    mxb_assert(!reply.error());
    GWBUF* rval = nullptr;

    if (!reply.row_data().empty())
    {
        mxb_assert(reply.row_data().size() == 1);
        mxb_assert(reply.row_data().front().size() == 1);

        m_gtid_pos.parse(reply.row_data().front().front());
    }

    if (reply.is_complete())
    {
        mxb_assert_message(reply.rows_read() == 1, "The result should only have one row");
        m_wait_gtid = GTID_READ_DONE;
        MXB_INFO("GTID probe complete, GTID is: %s", m_gtid_pos.to_string().c_str());

        // We need to return something for the upper layer, an OK packet should be adequate
        rval = modutil_create_ok();
    }

    gwbuf_free(buffer);
    return rval;
}
