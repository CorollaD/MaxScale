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

#include "readwritesplit.hh"
#include "rwsplitsession.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.hh>

using namespace maxscale;

void RWSplitSession::continue_large_session_write(GWBUF* querybuf, uint32_t type)
{
    for (auto backend : m_raw_backends)
    {
        if (backend->in_use())
        {
            backend->write(gwbuf_clone_shallow(querybuf), mxs::Backend::NO_RESPONSE);
        }
    }
}

bool RWSplitSession::create_one_connection_for_sescmd()
{
    mxb_assert(can_recover_servers());

    // Try to first find a master if we are allowed to connect to one
    if (m_config.lazy_connect || m_config.master_reconnection)
    {
        for (auto backend : m_raw_backends)
        {
            if (!backend->in_use() && backend->can_connect() && backend->is_master())
            {
                if (prepare_target(backend, TARGET_MASTER))
                {
                    if (backend != m_current_master)
                    {
                        replace_master(backend);
                    }

                    MXB_INFO("Chose '%s' as primary due to session write", backend->name());
                    return true;
                }
            }
        }
    }

    // If no master was found, find a slave
    for (auto backend : m_raw_backends)
    {
        if (!backend->in_use() && backend->can_connect() && backend->is_slave())
        {
            if (prepare_target(backend, TARGET_SLAVE))
            {
                MXB_INFO("Chose '%s' as replica due to session write", backend->name());
                return true;
            }
        }
    }

    // No servers are available
    return false;
}
