/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    methods: {
        async asyncEmit(method, ...params) {
            const listener = this.$listeners[method]
            if (listener) {
                const res = await listener(...params)
                return res === undefined || res
            }
            return false
        },
    },
}
