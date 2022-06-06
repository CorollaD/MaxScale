/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include "common.hh"
#include "mirror.hh"
#include "mirrorbackend.hh"

#include <maxscale/backend.hh>
#include <maxscale/buffer.hh>

#include <deque>

class Mirror;

class MirrorSession : public mxs::RouterSession
{
public:
    MirrorSession(const MirrorSession&) = delete;
    MirrorSession& operator=(const MirrorSession&) = delete;

    MirrorSession(MXS_SESSION* session, Mirror* router, SMyBackends backends);

    ~MirrorSession();

    bool routeQuery(GWBUF* pPacket) override;

    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* pMessage,
                     mxs::Endpoint* pProblem, const mxs::Reply& pReply) override;

private:
    SMyBackends             m_backends;
    MyBackend*              m_main = nullptr;
    int                     m_responses = 0;
    Mirror*                 m_router;
    std::deque<mxs::Buffer> m_queue;
    std::string             m_query;
    uint8_t                 m_command = 0;
    uint64_t                m_num_queries = 0;
    mxs::Buffer             m_last_chunk;
    mxs::ReplyRoute         m_last_route;

    void route_queued_queries();
    bool should_report() const;
    void generate_report();
    void finalize_reply();
};
