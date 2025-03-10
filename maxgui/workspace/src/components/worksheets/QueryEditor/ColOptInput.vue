<template>
    <v-combobox
        v-if="input.type === 'column_type'"
        v-model="input.value"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="input.type"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="input.enum_values"
        item-text="value"
        item-value="value"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :return-object="false"
        @input="onInput"
    />
    <v-select
        v-else-if="input.type === 'enum'"
        v-model="input.value"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="input.type"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="input.enum_values"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :disabled="isDisabled"
        @input="onInput"
    />
    <v-checkbox
        v-else-if="input.type === 'bool'"
        v-model="input.value"
        dense
        class="v-checkbox--mariadb-xs ma-0 pa-0"
        primary
        hide-details
        :disabled="isDisabled"
        @change="onInput"
    />
    <charset-collate-select
        v-else-if="input.type === 'charset' || input.type === 'collation'"
        v-model="input.value"
        :items="
            input.type === 'charset'
                ? Object.keys(charsetCollationMap)
                : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
        "
        :defItem="input.type === 'charset' ? defTblCharset : defTblCollation"
        :disabled="isDisabled"
        :height="height"
        @input="onInput"
    />
    <v-text-field
        v-else
        v-model="input.value"
        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
        :class="`${input.type}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
        @input="onInput"
    />
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
 *
 * data: {
 *  field?: string, header name
 *  value?: string, cell value
 *  rowObj?: object, entire row data
 *  alterColIdx?: number, index of the column being altered
 *  colOptIdx?: number, index of the column option. e.g. index of PK, NN, UN ,...
 * }
 * Events
 * Below events are used to handle "coupled case",
 * e.g. When column_type changes its value to a data type
 * that supports charset/collation, `on-input-column_type`
 * will be used to update charset/collation input to fill
 * data with default table charset/collation.
 * on-input-column_type: (cell)
 * on-input-charset: (cell)
 * on-input-AI: (cell)
 * on-input-PK: (cell)
 * on-input-NN: (cell)
 * on-input-generated: (cell)
 * Event for normal cell
 * on-input: (cell)
 */

import CharsetCollateSelect from '@wkeComps/QueryEditor/CharsetCollateSelect.vue'
import {
    check_charset_support,
    check_UN_ZF_support,
    check_AI_support,
} from '@wkeComps/QueryEditor/alterTblHelpers'
export default {
    name: 'col-opt-input',
    components: {
        CharsetCollateSelect,
    },
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        charsetCollationMap: { type: Object, required: true },
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
        dataTypes: { type: Array, default: () => [] },
        initialColOptsData: { type: Array, required: true },
    },
    data() {
        return {
            input: {},
        }
    },
    computed: {
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.input, this.data)
        },
        columnCharset() {
            return this.$typy(this.input, 'rowObj.charset').safeString
        },
        columnType() {
            return this.$typy(this.input, 'rowObj.column_type').safeString
        },
        isPK() {
            return this.$typy(this.input, 'rowObj.PK').safeString === 'YES'
        },
        isGenerated() {
            return this.$typy(this.input, 'rowObj.generated').safeString !== '(none)'
        },
        isAI() {
            return this.$typy(this.input, 'rowObj.AI').safeString === 'AUTO_INCREMENT'
        },
        uniqueIdxName() {
            // If there's name already, use it otherwise generate one with this pattern `columnName_UNIQUE`
            const uqIdxName = this.$typy(this.initialColOptsData, `['${this.data.colOptIdx}']`)
                .safeString
            if (uqIdxName) return uqIdxName
            return `${this.$typy(this.data, 'rowObj.column_name').safeString}_UNIQUE`
        },
        isDisabled() {
            switch (this.$typy(this.input, 'field').safeString) {
                case 'charset':
                case 'collation':
                    if (this.columnCharset === 'utf8') return true
                    return !check_charset_support(this.columnType)
                case 'PK':
                    //disable if column is generated
                    return this.isGenerated
                case 'UN':
                case 'ZF':
                    return !check_UN_ZF_support(this.columnType)
                case 'AI':
                    return !check_AI_support(this.columnType)
                case 'NN':
                    //isAI or isPK implies NOT NULL so must be disabled
                    // when column is generated, NN or NULL can not be defined
                    return this.isAI || this.isPK || this.isGenerated
                case 'UQ':
                    return this.isPK // implies UNIQUE already so UQ must be disabled
                case 'generated':
                    //disable if column is PK
                    return this.isPK //https://mariadb.com/kb/en/generated-columns/#index-support
                default:
                    return false
            }
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.initInputType(v)
            },
        },
    },
    created() {
        this.initInputType(this.data)
    },
    methods: {
        initInputType(data) {
            this.input = this.handleAddType(data)
        },
        /**
         * This function handles adding type and necessary attribute to input
         * base on field name
         * @param {Object} data - initial input data
         * @returns {Object} - returns copied of data with necessary properties to render
         * appropriate input type
         */
        handleAddType(data) {
            const input = this.$helpers.lodash.cloneDeep(data)
            switch (input.field) {
                case 'column_name':
                    input.type = 'string'
                    break
                case 'column_type':
                    input.type = 'column_type'
                    input.enum_values = this.dataTypes
                    break
                case 'NN':
                    input.type = 'bool'
                    input.value = input.value === 'NOT NULL'
                    break
                case 'UN':
                    input.type = 'bool'
                    input.value = input.value === 'UNSIGNED'
                    break
                case 'ZF':
                    input.type = 'bool'
                    input.value = input.value === 'ZEROFILL'
                    break
                case 'AI':
                    input.type = 'bool'
                    input.value = input.value === 'AUTO_INCREMENT'
                    break
                case 'generated':
                    input.type = 'enum'
                    input.enum_values = ['(none)', 'VIRTUAL', 'STORED']
                    break
                case 'PK':
                    input.type = 'bool'
                    input.value = input.value === 'YES'
                    break
                case 'UQ':
                    input.type = 'bool'
                    input.value = Boolean(input.value)
                    break
                case 'charset':
                case 'collation':
                    input.type = input.field
                    break
            }
            return input
        },
        /**
         * This function basically undo what handleAddType did
         * @returns {Object} - returns input object with same properties as data props
         */
        handleRemoveType() {
            const newInput = this.$helpers.lodash.cloneDeep(this.input)
            delete newInput.type
            delete newInput.enum_values
            return newInput
        },
        onInput() {
            if (this.hasChanged) {
                let newInput = this.handleRemoveType()
                switch (this.input.type) {
                    case 'column_type':
                        this.$emit('on-input-column_type', newInput)
                        break
                    case 'charset':
                        this.$emit('on-input-charset', newInput)
                        break
                    case 'enum':
                        if (newInput.field === 'generated')
                            this.$emit('on-input-generated', newInput)
                        else this.$emit('on-input', newInput)
                        break
                    case 'bool': {
                        const field = newInput.field
                        switch (field) {
                            case 'NN':
                                newInput.value = newInput.value ? 'NOT NULL' : 'NULL'
                                break
                            case 'UN':
                                newInput.value = newInput.value ? 'UNSIGNED' : ''
                                break
                            case 'ZF':
                                newInput.value = newInput.value ? 'ZEROFILL' : ''
                                break
                            case 'AI':
                                newInput.value = newInput.value ? 'AUTO_INCREMENT' : ''
                                break
                            case 'PK':
                                newInput.value = newInput.value ? 'YES' : 'NO'
                                break
                            case 'UQ':
                                newInput.value = newInput.value ? this.uniqueIdxName : ''
                        }
                        if (field === 'AI') this.$emit('on-input-AI', newInput)
                        else if (field === 'PK') this.$emit('on-input-PK', newInput)
                        else if (field === 'NN') this.$emit('on-input-NN', newInput)
                        else this.$emit('on-input', newInput)
                        break
                    }
                    default:
                        this.$emit('on-input', newInput)
                        break
                }
            }
        },
    },
}
</script>
