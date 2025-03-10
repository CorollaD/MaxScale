<template>
    <page-wrapper>
        <v-sheet v-if="!$helpers.lodash.isEmpty(current_server)" class="pl-6">
            <page-header
                :currentServer="current_server"
                :onEditSucceeded="dispatchFetchServer"
                @on-count-done="fetchAll"
            />
            <overview-header
                :currentServer="current_server"
                :getRelationshipData="getRelationshipData"
                @on-relationship-update="dispatchRelationshipUpdate"
            />
            <v-tabs v-model="currentActiveTab" class="v-tabs--mariadb">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col cols="4">
                                <v-row>
                                    <v-col cols="12">
                                        <details-readonly-table
                                            ref="statistics-table"
                                            :title="`${$mxs_tc('statistics', 2)}`"
                                            :tableData="serverStats"
                                            isTree
                                        />
                                    </v-col>
                                    <v-col cols="12">
                                        <relationship-table
                                            relationshipType="services"
                                            addable
                                            removable
                                            :tableRows="serviceTableRow"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                            <v-col cols="8">
                                <sessions-table
                                    collapsible
                                    delayLoading
                                    :items="sessionsTableRow"
                                    :server-items-length="getTotalFilteredSessions"
                                    @get-data-from-api="fetchSessionsWithFilter(filterSessionParam)"
                                    @confirm-kill="
                                        killSession({
                                            id: $event.id,
                                            callback: fetchSessionsWithFilter(filterSessionParam),
                                        })
                                    "
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col cols="6">
                                <details-parameters-table
                                    :resourceId="current_server.id"
                                    :parameters="current_server.attributes.parameters"
                                    usePortOrSocket
                                    :updateResourceParameters="updateServerParameters"
                                    :onEditSucceeded="dispatchFetchServer"
                                />
                            </v-col>
                            <v-col cols="6">
                                <details-readonly-table
                                    ref="diagnostics-table"
                                    :title="`${$mxs_t('monitorDiagnostics')}`"
                                    :tableData="monitorDiagnostics"
                                    expandAll
                                    isTree
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-sheet>
    </page-wrapper>
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
import { mapActions, mapMutations, mapState, mapGetters } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'

export default {
    name: 'server-detail',
    components: {
        PageHeader,
        OverviewHeader,
    },
    data() {
        return {
            currentActiveTab: null,
            tabs: [
                { name: `${this.$mxs_tc('statistics', 2)} & ${this.$mxs_tc('sessions', 2)}` },
                { name: `${this.$mxs_tc('parameters', 2)} & ${this.$mxs_tc('diagnostics', 2)}` },
            ],
            serviceTableRow: [],
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            current_server: state => state.server.current_server,
            monitor_diagnostics: state => state.monitor.monitor_diagnostics,
            filtered_sessions: state => state.session.filtered_sessions,
        }),
        ...mapGetters({
            getTotalFilteredSessions: 'session/getTotalFilteredSessions',
            getFilterParamByServerId: 'session/getFilterParamByServerId',
        }),
        serverStats() {
            return this.$typy(this.current_server, 'attributes.statistics').safeObjectOrEmpty
        },
        monitorDiagnostics() {
            const {
                attributes: { monitor_diagnostics: { server_info = [] } = {} } = {},
            } = this.monitor_diagnostics
            return server_info.find(server => server.name === this.$route.params.id) || {}
        },
        sessionsTableRow() {
            let tableRows = []
            this.filtered_sessions.forEach(session => {
                const {
                    id,
                    attributes: { idle, connected, user, remote, memory, io_activity },
                } = session
                tableRows.push({
                    id: id,
                    user: `${user}@${remote}`,
                    connected: this.$helpers.dateFormat({ value: connected }),
                    idle: idle,
                    memory,
                    io_activity,
                })
            })
            return tableRows
        },
        filterSessionParam() {
            return this.getFilterParamByServerId(this.$route.params.id)
        },
    },
    watch: {
        async should_refresh_resource(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.fetchAll()
            }
        },
        async currentActiveTab(val) {
            switch (val) {
                // when active tab is Parameters & Diagnostics
                case 1:
                    await this.fetchModuleParameters('servers')
                    break
            }
        },
        // re-fetch when the route changes
        async $route() {
            await this.fetchAll()
            if (this.currentActiveTab === 1) await this.fetchModuleParameters('servers')
        },
    },
    async created() {
        await this.fetchAll()
    },
    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
            SET_MONITOR_DIAGNOSTICS: 'monitor/SET_MONITOR_DIAGNOSTICS',
        }),
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchModuleParameters: 'fetchModuleParameters',
            fetchServerById: 'server/fetchServerById',
            updateServerRelationship: 'server/updateServerRelationship',
            updateServerParameters: 'server/updateServerParameters',
            fetchMonitorDiagnosticsById: 'monitor/fetchMonitorDiagnosticsById',
            fetchSessionsWithFilter: 'session/fetchSessionsWithFilter',
            killSession: 'session/killSession',
        }),
        async fetchAll() {
            await this.dispatchFetchServer()
            await Promise.all([
                this.serviceTableRowProcessing(),
                this.fetchSessionsWithFilter(this.filterSessionParam),
                this.fetchMonitorDiagnostics(),
            ])
        },
        async fetchMonitorDiagnostics() {
            const { relationships: { monitors = {} } = {} } = this.current_server
            if (monitors.data) {
                const monitorId = monitors.data[0].id
                await this.fetchMonitorDiagnosticsById(monitorId)
            } else {
                this.SET_MONITOR_DIAGNOSTICS({})
            }
        },
        // reuse functions for fetch loop or after finish editing
        async dispatchFetchServer() {
            await this.fetchServerById(this.$route.params.id)
        },

        async serviceTableRowProcessing() {
            const {
                relationships: { services: { data: servicesData = [] } = {} } = {},
            } = this.current_server
            let arr = []
            for (const service of servicesData) {
                const data = await this.getRelationshipData('services', service.id)
                const { id, type, attributes: { state = null } = {} } = data
                arr.push({ id, state, type })
            }
            this.serviceTableRow = arr
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource: services, monitors
         * @param {String} id name of the resource (optional)
         * @return {Array} Resource state data
         */
        async getRelationshipData(type, id) {
            const data = await this.getResourceState({
                resourceId: id,
                resourceType: type,
            })
            return data
        },
        // actions to vuex
        async dispatchRelationshipUpdate({ type, data }) {
            await this.updateServerRelationship({
                id: this.current_server.id,
                type,
                [type]: data,
                callback: this.dispatchFetchServer,
            })
            switch (type) {
                case 'monitors':
                    await this.fetchMonitorDiagnostics()
                    break
                case 'services':
                    await this.serviceTableRowProcessing()
                    break
            }
        },
    },
}
</script>
