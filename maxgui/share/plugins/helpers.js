/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import * as helpers from '@share/utils/helpers'
export default {
    /**
     *
     * @param {Object} param.addon - more helpers to be merged
     */
    install: (Vue, { addon = {} }) => {
        Vue.prototype.$helpers = { ...helpers, ...addon }
    },
}
