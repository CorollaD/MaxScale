/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#define MXS_MODULE_NAME "cache"
#include "cachest.hh"
#include "storage.hh"
#include "storagefactory.hh"

using std::shared_ptr;

CacheST::CacheST(const std::string& name,
                 const CacheConfig* pConfig,
                 const std::vector<SCacheRules>& rules,
                 SStorageFactory sFactory,
                 Storage* pStorage)
    : CacheSimple(name, pConfig, rules, sFactory, pStorage)
{
    MXS_NOTICE("Created single threaded cache.");
}

CacheST::~CacheST()
{
}

CacheST* CacheST::create(const std::string& name, const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CacheST* pCache = NULL;

    std::vector<SCacheRules> rules;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::get_storage_factory(*pConfig, &rules, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = create(name, pConfig, rules, sFactory);
    }

    return pCache;
}

// static
CacheST* CacheST::create(const std::string& name,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory sFactory,
                         const CacheConfig* pConfig)
{
    mxb_assert(sFactory.get());
    mxb_assert(pConfig);

    return create(name, pConfig, rules, sFactory);
}

json_t* CacheST::get_info(uint32_t flags) const
{
    return CacheSimple::do_get_info(flags);
}

bool CacheST::must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    return CacheSimple::do_must_refresh(key, pSession);
}

void CacheST::refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    CacheSimple::do_refreshed(key, pSession);
}

// static
CacheST* CacheST::create(const std::string& name,
                         const CacheConfig* pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory sFactory)
{
    CacheST* pCache = NULL;

    Storage::Config storage_config(CACHE_THREAD_MODEL_ST,
                                   pConfig->hard_ttl.count(),
                                   pConfig->soft_ttl.count(),
                                   pConfig->max_count,
                                   pConfig->max_size,
                                   pConfig->invalidate,
                                   pConfig->timeout);

    const auto& storage_arguments = pConfig->storage_options;

    Storage* pStorage = sFactory->create_storage(name.c_str(), storage_config, storage_arguments);

    if (pStorage)
    {
        MXS_EXCEPTION_GUARD(pCache = new CacheST(name,
                                                 pConfig,
                                                 rules,
                                                 sFactory,
                                                 pStorage));

        if (!pCache)
        {
            delete pStorage;
        }
    }

    return pCache;
}
