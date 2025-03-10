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
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_TMP_ENTITIES } from '@wsSrc/store/config'

export default class QueryEditorTmp extends Extender {
    static entity = ORM_TMP_ENTITIES.QUERY_EDITORS_TMP

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            // fields for SchemaSidebar
            loading_db_tree: this.boolean(false),
            completion_items: this.attr([]),
            db_tree_of_conn: this.string(''), // Name of the connection using to fetch data
            db_tree: this.attr([]), // Contains schemas array
            // Fields for QueryEditor
            /**
             * @property {object} data - Contains res.data.data.attributes of a query
             * @property {object} stmt_err_msg_obj
             */
            exe_stmt_result: this.attr({}),
        }
    }

    static fields() {
        return {
            id: this.attr(null), // use QueryEditor id as PK for this table
            ...this.getNonKeyFields(),
        }
    }
}
