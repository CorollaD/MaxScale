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

/**
 * Internal header for the monitor
 */

#include <maxscale/monitor.hh>

/**
 * Returns parameter definitions shared by all monitors.
 *
 * @return Common monitor parameters.
 */
const MXS_MODULE_PARAM* common_monitor_params();
