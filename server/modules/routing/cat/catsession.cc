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
#include "cat.hh"
#include "catsession.hh"

#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/modutil.hh>

using namespace maxscale;

CatSession::CatSession(MXS_SESSION* session, Cat* router, mxs::SRWBackends backends)
    : RouterSession(session)
    , m_backends(std::move(backends))
    , m_completed(0)
    , m_packet_num(0)
    , m_query(NULL)
{
}

CatSession::~CatSession()
{
    for (auto& backend : m_backends)
    {
        if (backend->in_use())
        {
            backend->close();
        }
    }
}

bool CatSession::next_backend()
{
    // Skip unused backends
    while (m_current != m_backends.end() && !(*m_current)->in_use())
    {
        m_current++;
    }

    return m_current != m_backends.end();
}

bool CatSession::routeQuery(GWBUF* pPacket)
{
    int32_t rval = 0;

    m_completed = 0;
    m_packet_num = 0;
    m_query = pPacket;
    m_current = m_backends.begin();

    if (next_backend())
    {
        // We have a backend, write the query only to this one. It will be
        // propagated onwards in clientReply.
        rval = (*m_current)->write(gwbuf_clone_shallow(pPacket));
    }

    return rval;
}

bool CatSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    auto& backend = *m_current;
    mxb_assert(backend->backend() == down.back());
    bool send = false;

    if (reply.is_complete())
    {
        m_completed++;
        m_current++;

        if (!next_backend())
        {
            send = true;
            gwbuf_free(m_query);
            m_query = NULL;
        }
        else
        {
            (*m_current)->write(gwbuf_clone_shallow(m_query));
        }
    }

    if (m_completed == 0)
    {
        send = reply.state() != mxs::ReplyState::DONE;
    }
    else if (reply.state() == mxs::ReplyState::RSET_ROWS
             && mxs_mysql_get_command(pPacket) != MYSQL_REPLY_EOF)
    {
        send = true;
    }

    int32_t rc = 1;

    if (send)
    {
        // Increment the packet sequence number and send it to the client
        GWBUF_DATA(pPacket)[3] = m_packet_num++;
        rc = RouterSession::clientReply(pPacket, down, reply);
    }
    else
    {
        gwbuf_free(pPacket);
    }

    return rc;
}

bool CatSession::handleError(mxs::ErrorType type,
                             GWBUF* pMessage,
                             mxs::Endpoint* pProblem,
                             const mxs::Reply& pReply)
{
    /**
     * The simples thing to do here is to close the connection. Anything else
     * would still require extra processing on the client side and reconnecting
     * will cause things to fix themselves.
     */
    return false;
}
