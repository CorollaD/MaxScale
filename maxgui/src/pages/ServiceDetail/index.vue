<template>
    <page-wrapper>
        <v-sheet v-if="!$helpers.lodash.isEmpty(current_service)" class="pl-6">
            <page-header :currentService="current_service" :onEditSucceeded="fetchService">
                <template v-slot:refresh-rate>
                    <refresh-rate v-model="refreshRate" @on-count-done="onCountDone" />
                </template>
            </page-header>
            <overview-header
                ref="overviewHeader"
                :currentService="current_service"
                :serviceConnectionsDatasets="service_connections_datasets"
                :serviceConnectionInfo="serviceConnectionInfo"
                :refreshRate="refreshRate"
            />

            <v-tabs v-model="currentActiveTab" class="v-tabs--mariadb">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <!-- Parameters & relationships tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col cols="8">
                                <details-parameters-table
                                    :resourceId="current_service.id"
                                    :parameters="current_service.attributes.parameters"
                                    :updateResourceParameters="updateServiceParameters"
                                    :onEditSucceeded="fetchService"
                                />
                            </v-col>
                            <v-col cols="4">
                                <v-row>
                                    <v-col cols="12">
                                        <routing-target-table
                                            :routerId="current_service.id"
                                            :tableRows="routingTargetsTableRows"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12">
                                        <relationship-table
                                            ref="filters-relationship-table"
                                            relationshipType="filters"
                                            addable
                                            removable
                                            :tableRows="filtersTableRows"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12">
                                        <relationship-table
                                            ref="listeners-relationship-table"
                                            relationshipType="listeners"
                                            addable
                                            :tableRows="listenersTableRows"
                                            @open-listener-form-dialog="
                                                SET_FORM_TYPE(RESOURCE_FORM_TYPES.LISTENER)
                                            "
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Sessions & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col cols="5">
                                <details-readonly-table
                                    ref="diagnostics-table"
                                    :title="`${$mxs_t('routerDiagnostics')}`"
                                    :tableData="routerDiagnostics"
                                    expandAll
                                    isTree
                                />
                            </v-col>
                            <v-col cols="7">
                                <sessions-table
                                    collapsible
                                    delayLoading
                                    :items="sessionsTableRows"
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
import OverviewHeader from './OverviewHeader'
import PageHeader from './PageHeader'
import refreshRate from '@share/mixins/refreshRate'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
    },
    mixins: [refreshRate],
    data() {
        return {
            currentActiveTab: null,
            tabs: [
                {
                    name: `${this.$mxs_tc('parameters', 2)} & ${this.$mxs_tc('relationships', 2)}`,
                },
                { name: `${this.$mxs_tc('sessions', 2)} & ${this.$mxs_tc('diagnostics', 2)}` },
            ],
            routingTargetsTableRows: [],
            listenersTableRows: [],
            filtersTableRows: [],
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            should_refresh_resource: 'should_refresh_resource',
            current_service: state => state.service.current_service,
            service_connections_datasets: state => state.service.service_connections_datasets,
            filtered_sessions: state => state.session.filtered_sessions,
            RESOURCE_FORM_TYPES: state => state.app_config.RESOURCE_FORM_TYPES,
            ROUTING_TARGET_RELATIONSHIP_TYPES: state =>
                state.app_config.ROUTING_TARGET_RELATIONSHIP_TYPES,
        }),
        serviceId() {
            return this.$route.params.id
        },
        ...mapGetters({
            getTotalFilteredSessions: 'session/getTotalFilteredSessions',
            getFilterParamByServiceId: 'session/getFilterParamByServiceId',
        }),

        serviceConnectionInfo() {
            const { total_connections, connections } = this.$typy(
                this.current_service,
                'attributes.statistics'
            ).safeObjectOrEmpty
            return { total_connections, connections }
        },
        routerDiagnostics() {
            return this.$typy(this.current_service, 'attributes.router_diagnostics')
                .safeObjectOrEmpty
        },
        routerModule() {
            return this.current_service.attributes.router
        },
        sessionsTableRows() {
            return this.filtered_sessions.map(
                ({ id, attributes: { idle, connected, user, remote, memory, io_activity } }) => ({
                    id,
                    user: `${user}@${remote}`,
                    connected: this.$helpers.dateFormat({ value: connected }),
                    idle,
                    memory,
                    io_activity,
                })
            )
        },
        filterSessionParam() {
            return this.getFilterParamByServiceId(this.$route.params.id)
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
            // when active tab is Parameters & Relationships
            if (val === 0) await this.fetchModuleParameters(this.routerModule)
        },
        // re-fetch when the route changes
        async $route() {
            await this.fetchAll()
            if (this.currentActiveTab === 0) await this.fetchModuleParameters(this.routerModule)
        },
    },
    async created() {
        await this.fetchAll()
        // Generate datasets
        this.genServiceConnectionsDataSets()
    },
    methods: {
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchModuleParameters: 'fetchModuleParameters',
            fetchServiceById: 'service/fetchServiceById',
            genServiceConnectionsDataSets: 'service/genDataSets',
            updateServiceRelationship: 'service/updateServiceRelationship',
            updateServiceParameters: 'service/updateServiceParameters',
            fetchSessionsWithFilter: 'session/fetchSessionsWithFilter',
            fetchAllFilters: 'filter/fetchAllFilters',
            killSession: 'session/killSession',
        }),
        ...mapMutations({
            SET_FORM_TYPE: 'SET_FORM_TYPE',
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),

        async fetchAll() {
            await this.fetchService()
            await Promise.all([
                this.fetchSessionsWithFilter(this.filterSessionParam),
                this.processRoutingTargetsTable(),
                this.processRelationshipTable('filters'),
                this.processRelationshipTable('listeners'),
            ])
        },
        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.serviceId)
        },
        async onCountDone() {
            await this.fetchAll()
            await this.$refs.overviewHeader.updateChart()
        },
        async genRelationshipRows(type) {
            const { relationships: { [`${type}`]: { data = [] } = {} } = {} } = this.current_service
            let arr = []
            for (const obj of data) {
                const { attributes: { state = null } = {} } = await this.getRelationshipData(
                    type,
                    obj.id
                )
                let row = { id: obj.id, type, state }
                if (type === 'filters') delete row.state
                arr.push(row)
            }
            return arr
        },
        async processRoutingTargetsTable() {
            const { relationships = {} } = this.current_service
            let rows = []
            for (const key of Object.keys(relationships)) {
                if (this.ROUTING_TARGET_RELATIONSHIP_TYPES.includes(key)) {
                    const data = await this.genRelationshipRows(key)
                    rows = [...rows, ...data]
                }
            }
            this.routingTargetsTableRows = rows
        },
        /**
         * This function get relationship data based on relationship type i.e. filters, listeners.
         * It loops through id array to send sequential requests to get relationship state
         * @param {String} type- relationship type of resource. either filters or listeners
         */
        async processRelationshipTable(type) {
            this[`${type}TableRows`] = await this.genRelationshipRows(type)
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource: listeners, filters
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
        async dispatchRelationshipUpdate({ type, data, isFilterDrag, isUpdatingRouteTarget }) {
            await this.updateServiceRelationship({
                id: this.current_service.id,
                type: type,
                data,
                callback: this.fetchService,
            })
            switch (type) {
                case 'filters':
                    if (!isFilterDrag) await this.processRelationshipTable(type)
                    break
            }
            if (isUpdatingRouteTarget) this.processRoutingTargetsTable()
        },
    },
}
</script>
