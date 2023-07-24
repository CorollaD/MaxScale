/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#include <maxscale/filter.hh>
#include <string>

enum ds_state
{
    DS_STREAM_CLOSED,   /**< Initial state */
    DS_REQUEST_SENT,    /**< Request for stream sent */
    DS_REQUEST_ACCEPTED,/**< Stream request accepted */
    DS_STREAM_OPEN,     /**< Stream is open */
    DS_CLOSING_STREAM   /**< Stream is about to be closed */
};

class InsertStreamSession;

class InsertStream : public maxscale::Filter<InsertStream, InsertStreamSession>
{
public:
    InsertStream(const InsertStream&) = delete;
    InsertStream& operator=(const InsertStream&) = delete;

    static InsertStream* create(const char* zName, mxs::ConfigParameters* ppParams);
    InsertStreamSession* newSession(MXS_SESSION* pSession, SERVICE* pService);
    json_t*              diagnostics() const;
    uint64_t             getCapabilities();

private:
    InsertStream();
};

class InsertStreamSession : public maxscale::FilterSession
{
public:
    InsertStreamSession(const InsertStreamSession&) = delete;
    InsertStreamSession& operator=(const InsertStreamSession&) = delete;

    InsertStreamSession(MXS_SESSION* pSession, SERVICE* pService, InsertStream* filter);
    void close();
    int  routeQuery(GWBUF* pPacket);
    int  clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    InsertStream* m_filter;
    mxs::Buffer   m_queue;
    bool          m_active {true};              /**< Whether the session is active */
    uint8_t       m_packet_num;                 /**< If stream is open, the current packet sequence number */
    ds_state      m_state {DS_STREAM_CLOSED};   /**< The current state of the stream */
    std::string   m_target;                     /**< Current target table */
};
