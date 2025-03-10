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

// NOTE: Do not include <maxscale/ccdefs.hh>, it includes this.
#include <maxbase/ccdefs.hh>

#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <sstream>
#include <functional>
#include <set>

#if defined (MXS_MODULE_NAME)
#error In MaxScale >= 7, a module must not declare MXS_MODULE_NAME, but MXB_MODULE_NAME.
#endif

#if !defined (MXB_MODULE_NAME)
#define MXB_MODULE_NAME NULL
#endif

#include <maxbase/log.hh>
#include <maxbase/jansson.hh>
#include <maxbase/string.hh>

/**
 * Initializes MaxScale log manager
 *
 * @param ident  The syslog ident. If NULL, then the program name is used.
 * @param logdir The directory for the log file. If NULL, file output is discarded.
 * @param target Logging target
 *
 * @return true if succeed, otherwise false
 */
bool mxs_log_init(const char* ident, const char* logdir, mxb_log_target_t target);

/**
 * Close and reopen MaxScale log files. Also increments a global rotation counter which modules
 * can read to see if they should rotate their own logs.
 *
 * @return True if MaxScale internal logs were rotated. If false is returned, the rotation counter is not
 * incremented.
 */
bool mxs_log_rotate();

/**
 * Get the value of the log rotation counter. The counter is incremented when user requests a log rotation.
 *
 * @return Counter value
 */
int mxs_get_log_rotation_count();

/**
 * Get MaxScale logs as JSON
 *
 * @param host   The hostname of this MaxScale, sent by the client.
 *
 * @return The logs as a JSON API resource.
 */
json_t* mxs_logs_to_json(const char* host);

/**
 * Get MaxScale log data as JSON
 *
 * @param host     The hostname of this MaxScale, sent by the client.
 * @param cursor   The cursor where to read log entries for. An empty string means no cursor is open.
 * @param rows     How many rows of logs to read.
 * @param priority Log priorities to include or empty set for all priorities
 *
 * @return The log data as a JSON API resource.
 */
json_t* mxs_log_data_to_json(const char* host, const std::string& cursor, int rows,
                             const std::set<std::string>& priorities);

/**
 * Create a stream of logs
 *
 * TODO: This should be in an internal header
 *
 * @param cursor   The cursor where to stream the entries for. An empty cursor means start
 *                 from the latest position.
 * @param priority Log priorities to include or empty set for all priorities
 *
 * @return Function that can be called to read the log. If an empty string is returned, the current
 *         end of the log is reached. Calling it again can return more data at a later time.
 */
std::function<std::string()> mxs_logs_stream(const std::string& cursor,
                                             const std::set<std::string>& priorities);

inline void mxs_log_finish()
{
    mxb_log_finish();
}
