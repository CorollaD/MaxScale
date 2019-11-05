/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "inmemorystorage.hh"

class InMemoryStorageST : public InMemoryStorage
{
public:
    ~InMemoryStorageST();

    typedef std::auto_ptr<InMemoryStorageST> SInMemoryStorageST;

    static SInMemoryStorageST Create(const std::string& name,
                                     const Config& config,
                                     int argc,
                                     char* argv[]);

    cache_result_t get_info(uint32_t what, json_t** ppInfo) const override final;
    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppResult) override final;
    cache_result_t put_value(const CACHE_KEY& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue) override final;
    cache_result_t del_value(const CACHE_KEY& key) override final;
    cache_result_t invalidate(const std::vector<std::string>& words) override final;
    cache_result_t clear() override final;

private:
    InMemoryStorageST(const std::string& name, const Config& config);

private:
    InMemoryStorageST(const InMemoryStorageST&);
    InMemoryStorageST& operator=(const InMemoryStorageST&);
};
