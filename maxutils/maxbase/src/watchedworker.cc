/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#include <maxbase/watchedworker.hh>

namespace maxbase
{

WatchedWorker::WatchedWorker(WatchdogNotifier* pNotifier)
    : WatchdogNotifier::Dependent(pNotifier)
{
}

WatchedWorker::~WatchedWorker()
{
}

void WatchedWorker::call_epoll_tick()
{
    mark_ticking_if_currently_not();

    epoll_tick();
}
}
