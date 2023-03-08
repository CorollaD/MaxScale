/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <map>
#include <maxscale/ccdefs.hh>
#include <maxscale/qc_stmt_info.hh>
#include <maxscale/parser.hh>

/**
 * qc_init_kind_t specifies what kind of initialization should be performed.
 */
enum qc_init_kind_t
{
    QC_INIT_SELF   = 0x01,  /*< Initialize/finalize the query classifier itself. */
    QC_INIT_PLUGIN = 0x02,  /*< Initialize/finalize the plugin. */
    QC_INIT_BOTH   = 0x03
};

/**
 * QUERY_CLASSIFIER defines the object a query classifier plugin must
 * implement and return.
 *
 * To a user of the query classifier functionality, it can in general
 * be ignored.
 */
class QUERY_CLASSIFIER
{
public:
    /**
     * Called once to setup the query classifier
     *
     * @param sql_mode  The default sql mode.
     * @param args      The value of `query_classifier_args` in the configuration file.
     *
     * @return QC_RESULT_OK, if the query classifier could be setup, otherwise
     *         some specific error code.
     */
    virtual int32_t setup(qc_sql_mode_t sql_mode, const char* args) = 0;

    /**
     * Called once per each thread.
     *
     * @return QC_RESULT_OK, if the thread initialization succeeded.
     */
    virtual int32_t thread_init(void) = 0;

    /**
     * Called once when a thread finishes.
     */
    virtual void thread_end(void) = 0;

    /**
     * Return statement currently being classified.
     *
     * @param ppStmp  Pointer to pointer that on return will point to the
     *                statement being classified.
     * @param pLen    Pointer to value that on return will contain the length
     *                of the returned string.
     *
     * @return QC_RESULT_OK if a statement was returned (i.e. a statement is being
     *         classified), QC_RESULT_ERROR otherwise.
     */
    virtual int32_t get_current_stmt(const char** ppStmt, size_t* pLen) = 0;

    /**
     * Get result from info.
     *
     * @param  The info whose result should be returned.
     *
     * @return The result of the provided info.
     */
    virtual QC_STMT_RESULT get_result_from_info(const QC_STMT_INFO* info) = 0;

    /**
     * Get canonical statement
     *
     * @param info  The info whose canonical statement should be returned.
     *
     * @attention - The string_view refers to data that remains valid only as long
     *              as @c info remains valid.
     *            - If @c info is of a COM_STMT_PREPARE, then the canonical string will
     *              be suffixed by ":P".
     *
     * @return The canonical statement.
     */
    virtual std::string_view info_get_canonical(const QC_STMT_INFO* info) = 0;

    virtual mxs::Parser& parser() = 0;
};

/**
 * Loads a particular query classifier.
 *
 * In general there is no need to use this function, but rely upon qc_init().
 * However, if there is a need to use multiple query classifiers concurrently
 * then this function provides the means for that. Note that after a query
 * classifier has been loaded, it must explicitly be initialized before it
 * can be used.
 *
 * @param plugin_name  The name of the plugin from which the query classifier
 *                     should be loaded.
 *
 * @return A QUERY_CLASSIFIER object if successful, NULL otherwise.
 *
 * @see qc_unload
 */
QUERY_CLASSIFIER* qc_load(const char* plugin_name);

/**
 * Unloads an explicitly loaded query classifier.
 *
 * @see qc_load
 */
void qc_unload(QUERY_CLASSIFIER* classifier);

/**
 * Common query classifier properties as JSON.
 *
 * @param zHost  The MaxScale host.
 *
 * @return A json object containing properties.
 */
std::unique_ptr<json_t> qc_as_json(const char* zHost);

/**
 * Alter common query classifier properties.
 *
 * @param pJson  A JSON object.
 *
 * @return True, if the object was valid and parameters could be changed,
 *         false otherwise.
 */
bool qc_alter_from_json(json_t* pJson);

/**
 * Return query classifier cache content.
 *
 * @param zHost      The MaxScale host.
 *
 * @return A json object containing information about the query classifier cache.
 */
std::unique_ptr<json_t> qc_cache_as_json(const char* zHost);

/**
 * Classify statement
 *
 * @param zHost      The MaxScale host.
 * @param statement  The statement to be classified.
 *
 * @return A json object containing information about the statement.
 */
std::unique_ptr<json_t> qc_classify_as_json(const char* zHost, const std::string& statement);
