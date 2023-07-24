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

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

/**
 * @brief Return a pseudo-random number
 *
 * Return a pseudo-random number that satisfies major tests for random sequences.
 *
 * @return A random number
 */
unsigned int mxs_random(void);

MXS_END_DECLS
