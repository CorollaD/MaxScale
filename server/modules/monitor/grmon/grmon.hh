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

#include <maxscale/ccdefs.hh>
#include <maxscale/monitor.hh>

/**
 * @file grmon.hh A MySQL Group Replication cluster monitor
 */

class GRMon : public maxscale::MonitorWorkerSimple
{
public:
    GRMon(const GRMon&) = delete;
    GRMon& operator&(const GRMon&) = delete;

    ~GRMon();
    static GRMon* create(const std::string& name, const std::string& module);

protected:
    bool has_sufficient_permissions() override;
    void update_server_status(mxs::MonitorServer* monitored_server) override;

private:
    GRMon(const std::string& name, const std::string& module);
};
