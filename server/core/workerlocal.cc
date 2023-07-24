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

#include <maxscale/workerlocal.hh>

namespace
{

class FunctionTask : public mxb::Worker::DisposableTask
{
public:
    FunctionTask(std::function<void ()> cb)
        : m_cb(cb)
    {
    }

    void execute(mxb::Worker& worker)
    {
        m_cb();
    }

protected:
    std::function<void ()> m_cb;
};

}

namespace maxscale
{

void worker_local_delete_data(uint64_t key)
{
    auto func = [key]() {
        mxs::RoutingWorker::get_current()->storage().delete_data(key);
    };

    std::unique_ptr<FunctionTask> task(new FunctionTask(func));
    mxs::RoutingWorker::broadcast(std::move(task));
}

}
