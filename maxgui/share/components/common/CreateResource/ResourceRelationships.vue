<template>
    <mxs-collapse
        wrapperClass="mt-4"
        titleWrapperClass="mx-n9"
        :toggleOnClick="() => (showContent = !showContent)"
        :isContentVisible="showContent"
        :title="`${$mxs_tc(relationshipsType, multiple ? 2 : 1)}`"
    >
        <mxs-select
            v-model="selectedItems"
            :defaultItems="defaultItems"
            :items="items"
            :entityName="relationshipsType"
            :multiple="multiple"
            :clearable="clearable"
            :required="required"
        />
    </mxs-collapse>
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
This component takes items array props to render v-select component for selecting relationship data.
Eg : items=[{id:'row_server_1', type:'servers'}]
relationshipsType props is to defined to render correct text, display what relationship type is being target
When getSelectedItems is called by parent component, it returns selectedItems as an array regardless the value
of multiple props since the valid value for resource relationship must be an array
*/
export default {
    name: 'resource-relationships',
    props: {
        relationshipsType: { type: String, required: true },
        items: { type: Array, required: true },
        multiple: { type: Boolean, default: true },
        clearable: { type: Boolean, default: false },
        required: { type: Boolean, default: false },
        defaultItems: { type: [Array, Object], default: () => [] },
    },

    data: function() {
        return {
            showContent: true,
            selectedItems: [],
        }
    },
    methods: {
        getSelectedItems() {
            if (this.$typy(this.selectedItems).isNull) return []
            if (this.$typy(this.selectedItems).isArray) return this.selectedItems
            return [this.selectedItems]
        },
    },
}
</script>
