<template>
    <div>
        <v-data-table
            v-sortable-table
            :headers="headers"
            :items="!loading ? tableRows : []"
            :hide-default-header="true"
            :hide-default-footer="showAll ? true : tableRows.length <= 10"
            :items-per-page="showAll ? -1 : itemsPerPage"
            :class="['data-table-full', tableClass]"
            :loading="loading"
            :options.sync="pagination"
            :page="page"
            :sort-by="sortBy"
            :sort-desc="sortDesc"
            :fixed-header="fixedHeader"
            :height="height"
            :search="search"
            item-key="id"
            :dense="dense"
            :no-data-text="noDataText"
            :custom-sort="customSort"
            :custom-filter="customFilter"
            @current-items="colsHasRowSpan ? getCurrentItems : null"
            @on-drag-end="draggable ? $emit('on-drag-end', $event) : null"
        >
            <!----------------------------------------------------TABLE HEAD------------------------------------------>
            <template v-slot:header="{ props: { headers } }">
                <table-header
                    :headers="headers"
                    :sortDesc="pagination.sortDesc.length ? pagination.sortDesc[0] : false"
                    :sortBy="pagination.sortBy.length ? pagination.sortBy[0] : ''"
                    :isTree="isTree"
                    :hasValidChild="hasValidChild"
                    @change-sort="changeSort"
                >
                    <template v-for="header in headers" :slot="`header-append-${header.value}`">
                        <slot :name="`header-append-${header.value}`"></slot>
                    </template>
                </table-header>
            </template>

            <template v-slot:item="{ item, index: rowIndex }">
                <table-row
                    :key="item.nodeId || item.id || rowIndex"
                    :rowIndex="rowIndex"
                    :editableCell="editableCell"
                    :draggable="draggable"
                    :showActionsOnHover="showActionsOnHover"
                >
                    <template v-slot:cell="{ data: { indexOfHoveredRow } }">
                        <table-cell
                            v-for="(header, cellIndex) in headers"
                            :key="cellIndex"
                            :ref="setRowspanRef({ cellIndex, item })"
                            :cellIndex="cellIndex"
                            :colsHasRowSpan="colsHasRowSpan"
                            :item="item"
                            :header="header"
                            :indexOfLastColumn="headers.length - 1"
                            :rowIndex="rowIndex"
                            :hasOrderNumber="hasOrderNumber"
                            :editableCell="editableCell"
                            :tdBorderLeft="tdBorderLeft"
                            :draggable="draggable"
                            :indexOfHoveredRow="indexOfHoveredRow"
                            :isTree="isTree"
                            :hasValidChild="hasValidChild"
                            :componentId="componentId"
                            @cell-hover="cellHover"
                            @get-truncated-info="truncatedMenu = $event"
                            @toggle-node="toggleNode"
                        >
                            <template :slot="header.value">
                                <slot
                                    :name="header.value"
                                    :data="{ item, header, cellIndex, rowIndex }"
                                >
                                    <span
                                        v-mxs-highlighter="{
                                            keyword: search,
                                            txt: getValue(item, header),
                                        }"
                                    >
                                        {{ getValue(item, header) }}
                                    </span>
                                </slot>
                            </template>
                            <template :slot="`${header.value}-append`">
                                <slot
                                    :name="`${header.value}-append`"
                                    :data="{ item, header, cellIndex, rowIndex }"
                                />
                            </template>
                            <template slot="actions">
                                <slot name="actions" :data="{ item }" />
                            </template>
                        </table-cell>
                    </template>
                </table-row>
            </template>
        </v-data-table>
        <v-menu
            v-if="truncatedMenu"
            :key="`.row-${truncatedMenu.rowIndex}_cell-${truncatedMenu.cellIndex}_${componentId}`"
            :value="Boolean(truncatedMenu.item)"
            top
            transition="slide-y-transition"
            :close-on-content-click="false"
            open-on-hover
            offset-y
            content-class="shadow-drop mxs-color-helper text-navigation rounded-sm"
            :max-height="600"
            :activator="
                `.row-${truncatedMenu.rowIndex}_cell-${truncatedMenu.cellIndex}_${componentId}`
            "
        >
            <v-sheet v-if="truncatedMenu.item" class="mxs-truncate-tooltip-menu pa-4">
                <span class="text-body-2">
                    {{ getValue(truncatedMenu.item, truncatedMenu.header) }}
                </span>
            </v-sheet>
        </v-menu>
    </div>
</template>

<script>
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

/*
headers: {
  text: string,
  value: any,
  width?: string,
  sortable?: boolean
  editableCol?: boolean, if true, apply editable style for that column
  autoTruncate?: boolean, auto truncate cell value
  align?: string, "center || left || right",
}

- data props as array of objects, each object must has either item.nodeId:Number || item.id:Any,
  if both presents nodeId will be used

SLOTS available for this component:
- slot :name="header.value" // slot aka item
- slot  name="actions" :data="{ item }"
- slot :name="`header-append-${header.value}`"

Emits:
- $emit('on-drag-end', event:Object)
- $emit('pagination', val:Object)
- $emit('cell-hover', { e, item, rowIndex, cellIndex, header })
*/
import Sortable from 'sortablejs'
import TableHeader from './TableHeader'
import TableCell from './TableCell'
import TableRow from './TableRow'

export default {
    name: 'data-table',
    components: {
        TableHeader,
        TableRow,
        TableCell,
    },
    directives: {
        sortableTable: {
            bind(el, binding, vnode) {
                const options = {
                    handle: '.drag-handle',
                    draggable: '.draggable-row',
                    animation: 200,
                    onEnd: function(event) {
                        vnode.child.$emit('on-drag-end', event)
                    },
                }
                Sortable.create(el.getElementsByTagName('tbody')[0], options)
            },
        },
    },
    props: {
        headers: { type: Array, required: true },
        data: { type: Array, required: true },
        sortBy: { type: String },
        search: { type: String, default: '' },
        sortDesc: { type: Boolean },
        loading: { type: Boolean, default: false },
        tableClass: { type: String },
        itemsPerPage: { type: Number, default: 10 },
        showAll: { type: Boolean, default: false },
        page: { type: Number, default: 1 },
        dense: { type: Boolean, default: false },
        noDataText: { type: String },
        fixedHeader: { type: Boolean, default: false },
        height: { type: [String, Number], default: 'unset' },
        // add border left to td
        tdBorderLeft: { type: Boolean, default: false },
        // For editable feature
        editableCell: { type: Boolean, default: false },
        pauseCompute: { type: Boolean, default: false },
        // For table wants to keep primitive value, eg:if it is false, null/undefined won't be displayed
        keepPrimitiveValue: { type: Boolean, default: false },
        // For draggable feature
        draggable: { type: Boolean, default: false },
        /*
        enable hasOrderNumber to display item index column, however,
        item needs to have its own index property because using table row index will not work properly
        when table items is being searched
         */
        hasOrderNumber: { type: Boolean, default: false },
        showActionsOnHover: { type: Boolean, default: false },
        // rowspan feature, data array must contains objects having groupId property.
        colsHasRowSpan: { type: Number, default: 0 },
        // if data has child object or array, enable this props in advance
        isTree: { type: Boolean, default: false },
        expandAll: { type: Boolean, default: false },
        customFilter: { type: Function },
    },
    data() {
        return {
            //common
            pagination: {},
            //For truncated cell
            truncatedMenu: null,
            //For nested data, display dropdown table row
            hasValidChild: false,
            nodeActiveIds: [],
            // this is needed when using custom activator in v-menu.
            componentId: this.$helpers.lodash.uniqueId('component_v-menu_'),
            processedData: [],
        }
    },
    computed: {
        tableRows() {
            if (this.isTree) {
                let newArr = []
                this.levelRecursive(this.processedData, newArr, this.nodeActiveIds)
                return newArr
            } else return this.processedData
        },
    },

    watch: {
        pagination: {
            handler(val) {
                this.$emit('pagination', val)
            },
            deep: true,
        },
        hasValidChild: function(val) {
            if (val && this.expandAll) this.expandAllNodes(this.tableRows)
        },
        data: {
            handler(v) {
                // processing data from data props to whether keepPrimitiveValue or not
                let data = this.$helpers.lodash.cloneDeep(v)
                if (!this.keepPrimitiveValue) {
                    data = data.map(obj => {
                        Object.keys(obj).forEach(
                            key => (obj[key] = this.$helpers.convertType(obj[key]))
                        )
                        return obj
                    })
                }
                if (!this.pauseCompute) this.processedData = data
            },
            deep: true,
            immediate: true,
        },
        processedData: {
            handler(newV, oV) {
                if (this.isTree && this.hasValidChild && this.expandAll)
                    if (!this.$helpers.lodash.isEqual(newV, oV))
                        // keep all nodes expanding when data props changes
                        this.expandAllNodes(this.tableRows)
            },
            deep: true,
            immediate: true,
        },
    },

    methods: {
        //---------------------------------Table events----------------------------------------------------------------
        cellHover({ e, item, rowIndex, cellIndex, header }) {
            this.$emit('cell-hover', {
                e,
                item,
                rowIndex,
                cellIndex,
                header,
                componentId: this.componentId,
            })

            if (this.colsHasRowSpan) {
                this.setRowspanBg(e, item, rowIndex, cellIndex)
            }
        },

        //---------------------------------Table helper functions ------------------------------------------
        getValue(item, header) {
            // data type shouldn't be handled here as it will break the filter result
            // use helper function to handle value before passing the data to table
            let value = item[header.value]
            return this.$typy(header.format).isFunction ? header.format(value) : `${value}`
        },

        //--------------------------------- @private Table sorting ---------------------------------------------------
        // Currently support sorting one column at a time
        customSort(items, sortBy, isDesc) {
            let result = items

            // if isTree, create a hash map for hierarchySort
            if (sortBy.length) {
                if (this.isTree) {
                    let hashMap = this.$helpers.hashMapByPath({ arr: items, path: 'parentNodeId' })
                    const firstKey = Object.keys(hashMap)[0]
                    result = this.hierarchySort({
                        hashMap,
                        key: firstKey,
                        sortBy,
                        isDesc,
                        result: [],
                    })
                } else result = items.sort((a, b) => this.sortOrder(a, b, isDesc, sortBy))
            }

            // if rowspan feature is enabled, processing sorted arr
            if (this.colsHasRowSpan && result.length) {
                const newArr = this.processingRowspanTable(result)
                result = newArr
            }

            return result
        },

        hierarchySort({ hashMap, key, sortBy, isDesc, result }) {
            if (hashMap[key] === undefined) return result
            let arr = hashMap[key].sort((a, b) => this.sortOrder(a, b, isDesc, sortBy))
            arr.forEach(obj => {
                result.push(obj)
                const key = obj.nodeId || obj.id
                this.hierarchySort({ hashMap, key, sortBy, isDesc, result })
            })
            return result
        },

        sortOrder(a, b, isDesc, sortBy) {
            if (isDesc[0]) {
                return b[sortBy] < a[sortBy] ? -1 : 1
            } else {
                return a[sortBy] < b[sortBy] ? -1 : 1
            }
        },

        changeSort(column) {
            // TODO: support multiple column sorting
            if (this.pagination.sortBy[0] === column) {
                this.pagination.sortDesc = [!this.pagination.sortDesc[0]]
            } else {
                this.pagination.sortBy = [column]
                this.pagination.sortDesc = [false]
            }
        },

        //--------------------------------- @private methods for Rowspan feature----------------------------------------
        setRowspanRef({ cellIndex, item }) {
            if (!this.colsHasRowSpan) return null
            return cellIndex < this.colsHasRowSpan
                ? `${item.groupId}RowspanCell`
                : `${item.groupId}Cell`
        },
        getCurrentItems(items) {
            // This ensure rowspan table feature works
            if (items.length) {
                this.processingRowspanTable(items, 'mutate')
            }
        },

        /**
         * @param {Array} data Array of objects
         * @param {String} mode mutate or undefined
         * @return {Array} Return new array if mode!=='mutate'
         * if mode ==='mutate', changes will be mutated, otherwise it returns a new array
         */
        processingRowspanTable(data, mode) {
            if (mode === 'mutate') this.handleDisplayRowspan(data)
            else return this.handleDisplayRowspan(this.$helpers.lodash.cloneDeep(data))
        },

        /**
         * This function group all items have same groupdID and assign
         * correct value for hidden and rowspan properties.
         * @param {Array} target Array of objects,
         * @return {Array} Always return new array
         */
        handleDisplayRowspan(target) {
            let uniqueSet = new Set(target.map(item => item.groupId))
            let itemsId = [...uniqueSet]

            let groupedId = this.$helpers.hashMapByPath({ arr: target, path: 'groupId' })
            let result = []
            for (let i = 0; i < itemsId.length; ++i) {
                let group = groupedId[`${itemsId[i]}`]

                for (let n = 0; n < group.length; ++n) {
                    group[n].rowspan = group.length
                    if (n === 0) group[n].hidden = false
                    else group[n].hidden = true
                    result.push(group[n])
                }
            }
            return result
        },

        /**
         * This function set background mxs-color-helper to rows when a cell or a row is hovered
         * It is used when rowspan feature is enabled
         * @param {Object} e event object
         * @param {Object} item object
         */
        setRowspanBg(e, item, rowIndex, cellIndex) {
            const target = cellIndex < this.colsHasRowSpan ? 'rowspanCell' : 'cell'
            const { groupId } = item
            // Make associated td elements to have the same hover effect
            let bg = e.type === 'mouseenter' ? '#fafcfc' : ''
            switch (target) {
                case 'cell':
                    {
                        let cellComponents = this.$refs[`${groupId}RowspanCell`]
                        cellComponents.forEach(ele => (ele.$el.style.backgroundColor = bg))
                    }
                    break
                case 'rowspanCell':
                    {
                        let cellComponents = this.$refs[`${groupId}Cell`]
                        cellComponents.forEach(ele => (ele.$el.style.backgroundColor = bg))
                    }
                    break
            }
        },

        //--------------------------------- @private methods: For nested data, displaying dropdown table row-----------
        /**
         * @param {Array} arr root array
         * @param {Array} newArr result array
         * @param {Array} nodeActiveIds array of active node ( node has been opened)
         * @return {Array} newArr new array that has element of root array and its children
         */
        levelRecursive(arr, newArr, nodeActiveIds) {
            let self = this
            arr.forEach(function(o) {
                if (o.children && o.children.length > 0) {
                    !self.hasValidChild && (self.hasValidChild = true)
                    newArr.push(o)
                    for (let i = 0; i < nodeActiveIds.length; ++i) {
                        if (o.nodeId === nodeActiveIds[i]) {
                            o.expanded = true
                        }
                    }
                    if (o.expanded === true) {
                        self.levelRecursive(o.children, newArr, nodeActiveIds)
                    }
                } else {
                    newArr.push(o)
                }
            })
        },

        toggleNode(node) {
            const self = this
            // expand node's children
            if (node.leaf === false && node.expanded === false && node.children.length > 0) {
                self.nodeActiveIds.push(node.nodeId)
                self.levelRecursive(node.children, [], self.nodeActiveIds, true)
            } else {
                // collapse node's children
                const isExpand = false
                this.toggleNodeChildren(node, isExpand)
            }
        },

        /**
         * @param {Object} node an object node
         * @param {Boolean} isExpand if it is true, it will expand the node
         * otherwise it collapse the node.
         */
        toggleNodeChildren(node, isExpand) {
            const self = this
            if (node.expanded === !isExpand && node.children.length > 0) {
                self.$set(node, 'expanded', isExpand)
                node.children.forEach(o => {
                    self.toggleNodeChildren(o, isExpand)
                })
                isExpand
                    ? self.nodeActiveIds.push(node.nodeId)
                    : self.nodeActiveIds.splice(self.nodeActiveIds.indexOf(node.nodeId), 1)
            }
        },

        /**
         * @param {Array} treeNodes treeNodes array processed by objToTree helper method
         */
        expandAllNodes(treeNodes) {
            const isExpand = true
            for (let i = 0; i < treeNodes.length; ++i) {
                let node = treeNodes[i]
                this.toggleNodeChildren(node, isExpand)
            }
        },
    },
}
</script>

<style lang="scss">
.draggable-row:hover {
    background: transparent !important;
}

.sortable-chosen:hover {
    background: #f2fcff !important;
    .drag-handle {
        display: inline;
    }
}
.sortable-ghost {
    background: #f2fcff !important;
    opacity: 0.6;
}
.v-data-table.data-table-full {
    table {
        thead {
            tr {
                box-shadow: -7px 5px 7px -7px rgba(0, 0, 0, 0.1);
                th {
                    text-transform: uppercase;
                    font-size: 11px;
                    white-space: nowrap;
                    &.active * {
                        color: black !important;
                    }
                    &:first-child {
                        border-radius: 5px 0 0 0;
                    }
                    &:last-child {
                        border-radius: 0 5px 0 0;
                    }
                }
                &:not(.v-data-table__progress) {
                    th {
                        padding: 0 25px;
                    }
                }
            }
        }

        tbody {
            tr:active:not(.v-data-table__expanded__content):not(.v-data-table__empty-wrapper):not(.v-data-table__editable-cell-mode) {
                background: #f2fcff !important;
            }
            tr {
                pointer-events: none !important;
                td {
                    pointer-events: all !important;
                    white-space: nowrap;
                    &:last-child:not(.hide) {
                        border-right: thin solid $table-border;
                    }
                    &:first-child:not(.hide) {
                        border-left: thin solid $table-border;
                    }
                }

                &:hover {
                    td.actions button {
                        opacity: 1;
                    }
                }
            }
            .v-data-table__empty-wrapper {
                td {
                    border-bottom: thin solid $table-border;
                }
            }
        }
    }
    .v-data-footer {
        border-top: thin solid rgba(0, 0, 0, 0) !important;
    }
}
</style>
