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

#include <maxbase/ccdefs.hh>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <maxbase/log.hh>

// TODO: Provide an MXB_DEBUG with the same meaning.
#if defined (SS_DEBUG)

#define mxb_assert(exp) \
    do {if (exp) {} else { \
            const char* debug_expr = #exp;      /** The MXB_ERROR marco doesn't seem to like stringification
                                                 * */ \
            MXB_ERROR("debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
            fprintf(stderr, "debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
            raise(SIGABRT);}} while (false)

#define mxb_assert_message(exp, fmt, ...) \
    do {if (exp) {} else {     \
            const char* debug_expr = #exp; \
            char message[1024]; \
            snprintf(message, sizeof(message), fmt, ##__VA_ARGS__); \
            MXB_ERROR("debug assert at %s:%d failed: %s (%s)\n", \
                      (char*)__FILE__, \
                      __LINE__, \
                      message, \
                      debug_expr); \
            fprintf(stderr, \
                    "debug assert at %s:%d failed: %s (%s)\n", \
                    (char*)__FILE__, \
                    __LINE__, \
                    message, \
                    debug_expr); \
            raise(SIGABRT);}} while (false)

#define MXB_AT_DEBUG(exp) exp

#else /* SS_DEBUG */

#define mxb_assert(exp)
#define mxb_assert_message(exp, fmt, ...)

#define MXB_AT_DEBUG(exp)

#endif /* SS_DEBUG */
