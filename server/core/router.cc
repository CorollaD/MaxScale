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

#include <maxscale/router.hh>
#include <maxscale/session.hh>

namespace maxscale
{

//
// RouterSession
//
RouterSession::RouterSession(MXS_SESSION* pSession)
    : m_pSession(pSession)
{
}

bool RouterSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return m_pUp->clientReply(pPacket, down, reply);
}

bool RouterSession::handleError(mxs::ErrorType type, GWBUF* pMessage, mxs::Endpoint* pProblem,
                                const mxs::Reply& pReply)
{
    return false;
}

void RouterSession::set_response(GWBUF* pResponse) const
{
    session_set_response(m_pSession, m_pUp, pResponse);
}
}
