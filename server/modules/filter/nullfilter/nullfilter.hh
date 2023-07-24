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
#include "nullfiltersession.hh"

class NullFilter : public maxscale::Filter<NullFilter, NullFilterSession>
{
public:
    NullFilter(const NullFilter&) = delete;
    NullFilter& operator=(const NullFilter&) = delete;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name);
        Config(Config&& other) = default;

        uint32_t capabilities;
    };

    ~NullFilter();
    static NullFilter* create(const char* zName, mxs::ConfigParameters* pParams);

    NullFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    json_t* diagnostics() const;

    uint64_t getCapabilities();

private:
    NullFilter(Config&& config);

private:
    Config m_config;
};
