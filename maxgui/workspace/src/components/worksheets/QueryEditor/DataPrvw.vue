<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <template v-if="activePrvwNodeQualifiedName">
                <div class="d-flex align-center mr-4">
                    <b class="mr-1">Table:</b>
                    <mxs-truncate-str
                        :tooltipItem="{ txt: activePrvwNodeQualifiedName, nudgeLeft: 16 }"
                        :maxWidth="260"
                    />
                </div>
                <data-prvw-nav-ctr :isLoading="isLoading" :resultData="resultData" />
                <v-spacer />
                <!-- Add activeQueryMode as key to make sure it re-render when switching between two tabs  -->
                <keep-alive>
                    <duration-timer
                        :key="activeQueryMode"
                        :startTime="requestSentTime"
                        :executionTime="execTime"
                        :totalDuration="totalDuration"
                    />
                </keep-alive>
            </template>
            <span v-else v-html="$mxs_t('prvwTabGuide')" />
        </div>
        <v-skeleton-loader
            v-if="isLoading"
            :loading="isLoading"
            type="table: table-thead, table-tbody"
            :height="dynDim.height - headerHeight"
        />
        <template v-else>
            <keep-alive>
                <result-data-table
                    v-if="$typy(resultData, 'fields').safeArray.length"
                    :key="activeQueryMode"
                    :height="dynDim.height - headerHeight"
                    :width="dynDim.width"
                    :headers="$typy(resultData, 'fields').safeArray.map(field => ({ text: field }))"
                    :rows="$typy(resultData, 'data').safeArray"
                    showGroupBy
                    v-on="$listeners"
                />
                <div v-else>
                    <div v-for="(v, key) in resultData" :key="key">
                        <b>{{ key }}:</b>
                        <span class="d-inline-block ml-4">{{ v }}</span>
                    </div>
                </div>
            </keep-alive>
        </template>
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
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer'
import DataPrvwNavCtr from '@wkeComps/QueryEditor/DataPrvwNavCtr.vue'

export default {
    name: 'data-prvw',
    components: {
        ResultDataTable,
        DurationTimer,
        DataPrvwNavCtr,
    },
    props: {
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
        activeQueryMode: { type: String, required: true },
        isLoading: { type: Boolean, required: true },
        data: { type: Object, required: true },
        requestSentTime: { type: Number, required: true },
        execTime: { type: Number, required: true },
        totalDuration: { type: Number, required: true },
        activePrvwNodeQualifiedName: { type: String, required: true },
    },
    data() {
        return {
            headerHeight: 0,
        }
    },
    computed: {
        resultData() {
            return this.$typy(this.data, 'data.attributes.results[0]').safeObjectOrEmpty
        },
    },
    activated() {
        this.setHeaderHeight()
    },
    methods: {
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
