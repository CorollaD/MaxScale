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
#pragma once

#include <maxscale/ccdefs.hh>

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <set>
#include <vector>

#include <maxscale/service.hh>

using namespace maxscale;

/** This contains the database to server mapping */
typedef std::unordered_map<std::string, std::unordered_map<std::string, std::set<mxs::Target*>>> ServerMap;
using TargetSet = std::set<mxs::Target*>;

typedef std::unordered_map<std::string, mxs::Target*> StmtMap;
typedef std::unordered_map<uint64_t, mxs::Target*>    BinaryPSMap;
typedef std::unordered_map<uint32_t, uint32_t>        PSHandleMap;

class Shard
{
public:
    Shard();
    ~Shard();

    /**
     * @brief Add a database location
     *
     * @param db     Database to add
     * @param target Target where database is located
     */
    void add_location(std::string db, std::string table, mxs::Target* target);

    /**
     * Same as get_location except returns all servers that have it
     */
    std::set<mxs::Target*> get_all_locations(std::string_view table_view);
    std::set<mxs::Target*> get_all_locations(QcTableName name);
    std::set<mxs::Target*> get_all_locations(std::string db, std::string tbl);
    template<class T>  // std::string_view or QcTableName
    std::set<mxs::Target*> get_all_locations(const std::vector<T>& db);

    void         add_statement(std::string stmt, mxs::Target* target);
    void         add_statement(uint32_t id, mxs::Target* target);
    mxs::Target* get_statement(std::string stmt);
    mxs::Target* get_statement(uint32_t id);
    bool         remove_statement(std::string stmt);
    bool         remove_statement(uint32_t id);

    void add_statement(std::string_view stmt, mxs::Target* target)
    {
        return add_statement(std::string(stmt), target);
    }

    mxs::Target* get_statement(std::string_view stmt)
    {
        return get_statement(std::string(stmt));
    }

    bool remove_statement(std::string_view stmt)
    {
        return remove_statement(std::string(stmt));
    }

    /**
     * @brief Check if shard contains stale information
     *
     * @param max_interval The maximum lifetime of the shard
     *
     * @return True if the shard is stale
     */
    bool stale(double max_interval) const;

    /**
     * @brief Check if shard is empty
     *
     * @return True if shard contains no locations
     */
    bool empty() const;

    /**
     * @brief Retrieve all database to server mappings
     *
     * @return The database to server mappings
     */
    const ServerMap& get_content() const;

    /**
     * @brief Check if the target is used by this shard
     *
     * @return Whether the target is used by this shard
     */
    bool uses_target(mxs::Target* target) const;

    /**
     * @brief Check if this shard is newer than the other shard
     *
     * @param shard The other shard to check
     *
     * @return True if this shard is newer
     */
    bool newer_than(const Shard& shard) const;

private:
    std::shared_ptr<ServerMap> m_map;
    std::shared_ptr<TargetSet> m_targets;
    StmtMap                    stmt_map;
    BinaryPSMap                m_binary_map;
    PSHandleMap                m_ps_handles;
    time_t                     m_last_updated;
};

typedef std::unordered_map<std::string, Shard>   ShardMap;
typedef std::unordered_map<std::string, int64_t> MapLimits;

class ShardManager
{
public:
    ShardManager();
    ~ShardManager();

    /**
     * @brief Retrieve or create a shard
     *
     * @param user         User whose shard to retrieve
     * @param max_lifetime The maximum lifetime of a shard
     *
     * @return The latest version of the shard or a newly created shard if no
     * old version is available
     */
    Shard get_shard(std::string user, double max_lifetime);

    /**
     * @brief Update the shard information
     *
     * The shard information is updated if the new shard contains more up to date
     * information than the one stored in the shard manager.
     *
     * @param shard New version of the shard
     * @param user  The user whose shard this is
     */
    void update_shard(Shard& shard, const std::string& user);

    /**
     * Set how many concurrent shard updates are allowed per user
     *
     * By default only one update per user is allowed.
     *
     * @param limit Number of concurrent users to allow
     */
    void set_update_limit(int64_t limit);

    /**
     * Start a shard update
     *
     * The update is considered finished when either update_shard() or cancel_update() is called. One of these
     * two must be called by the session once start_update() has returned true.
     *
     * @param user The user whose shard is about to be updated
     *
     * @return True if an update can be done. False if there are too many concurrent
     *         updates being done by this user.
     */
    bool start_update(const std::string& user);

    /**
     * Cancels a started shard update
     *
     * @param user The user whose shard was being updated
     */
    void cancel_update(const std::string& user);

private:
    mutable std::mutex m_lock;
    ShardMap           m_maps;
    MapLimits          m_limits;
    int64_t            m_update_limit {1};
};


template<class T>
std::set<mxs::Target*> Shard::get_all_locations(const std::vector<T>& tables)
{
    if (tables.empty())
    {
        return {};
    }

    auto it = tables.begin();
    std::set<mxs::Target*> targets = get_all_locations(*it++);

    for (; it != tables.end(); ++it)
    {
        std::set<mxs::Target*> right = get_all_locations(*it);
        std::set<mxs::Target*> left;
        left.swap(targets);
        std::set_intersection(right.begin(), right.end(), left.begin(), left.end(),
                              std::inserter(targets, targets.end()));
    }

    return targets;
}
