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

#define MXB_MODULE_NAME "cache"
#include "cache.hh"
#include <lzma.h>
#include <new>
#include <set>
#include <string>
#include <maxscale/buffer.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/paths.hh>
#include "storagefactory.hh"
#include "storage.hh"

using namespace std;

Cache::Cache(const std::string& name,
             const CacheConfig* pConfig,
             SStorageFactory sFactory)
    : m_name(name)
    , m_config(*pConfig)
    , m_sFactory(sFactory)
{
}

Cache::~Cache()
{
}

// static
bool Cache::get_storage_factory(const CacheConfig* pConfig,
                                StorageFactory** ppFactory)
{
    StorageFactory* pFactory = StorageFactory::open(pConfig->storage);

    if (pFactory)
    {
        *ppFactory = pFactory;
    }
    else
    {
        MXB_ERROR("Could not open storage factory '%s'.", pConfig->storage.c_str());
    }

    return pFactory != NULL;
}

json_t* Cache::show_json() const
{
    return get_info(INFO_ALL);
}

cache_result_t Cache::get_key(const std::string& user,
                              const std::string& host,
                              const char* zDefault_db,
                              const GWBUF* pQuery,
                              CacheKey* pKey) const
{
    return get_default_key(user, host, zDefault_db, pQuery, pKey);
}

// static
cache_result_t Cache::get_default_key(const std::string& user,
                                      const std::string& host,
                                      const char* zDefault_db,
                                      const GWBUF* pQuery,
                                      CacheKey* pKey)
{
    mxb_assert((user.empty() && host.empty()) || (!user.empty() && !host.empty()));

    const char* pSql;
    int length;

    modutil_extract_SQL(*pQuery, &pSql, &length);

    uint64_t crc = 0;

    const Bytef* pData;

    if (zDefault_db)
    {
        pData = reinterpret_cast<const uint8_t*>(zDefault_db);
        crc = lzma_crc64(pData, strlen(zDefault_db), crc);
    }

    pData = reinterpret_cast<const uint8_t*>(pSql);

    crc = lzma_crc64(pData, length, crc);

    pKey->data_hash = crc;

    if (!user.empty())
    {
        crc = lzma_crc64(reinterpret_cast<const uint8_t*>(user.data()), user.length(), crc);
    }

    pKey->user = user;

    if (!host.empty())
    {
        crc = lzma_crc64(reinterpret_cast<const uint8_t*>(host.data()), host.length(), crc);
    }

    pKey->host = host;

    pKey->full_hash = crc;

    return CACHE_RESULT_OK;
}

std::shared_ptr<CacheRules> Cache::should_store(const char* zDefaultDb, const GWBUF* pQuery)
{
    std::shared_ptr<CacheRules> sRules;

    auto sAll_rules = all_rules();

    const auto& rules = *sAll_rules.get();

    auto i = rules.begin();

    while (!sRules && (i != rules.end()))
    {
        if ((*i)->should_store(zDefaultDb, pQuery))
        {
            sRules = *i;
        }
        else
        {
            ++i;
        }
    }

    return sRules;
}

json_t* Cache::do_get_info(uint32_t what) const
{
    json_t* pInfo = json_object();

    if (pInfo)
    {
        if (what & INFO_RULES)
        {
            json_t* pArray = json_array();

            if (pArray)
            {
                auto sRules = all_rules();
                const auto& rules = *sRules.get();
                for (auto i = rules.begin(); i < rules.end(); ++i)
                {
                    json_t* pRules = const_cast<json_t*>((*i)->json());
                    json_array_append(pArray, pRules);      // Increases ref-count of pRules, we ignore
                                                            // failure.
                }

                json_object_set_new(pInfo, "rules", pArray);
            }
        }
    }

    return pInfo;
}


// static
uint64_t Cache::time_ms()
{
    timespec t;

    int rv = clock_gettime(CLOCK_MONOTONIC_COARSE, &t);
    if (rv != 0)
    {
        mxb_assert(errno == EINVAL);    // CLOCK_MONOTONIC_COARSE not supported.
        rv = clock_gettime(CLOCK_MONOTONIC, &t);
        mxb_assert(rv == 0);
    }

    return t.tv_sec * 1000 + (t.tv_nsec / 1000000);
}
