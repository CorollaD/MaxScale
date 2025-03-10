<template>
    <data-table
        :headers="tableHeaders"
        :data="tableRows"
        :colsHasRowSpan="2"
        :search="search_keyword"
        sortBy="groupId"
        :itemsPerPage="-1"
    >
        <template v-slot:header-append-groupId>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ monitorsLength }}) </span>
        </template>
        <template v-slot:header-append-id>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ all_servers.length }}) </span>
        </template>
        <template v-slot:header-append-serviceIds>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ servicesLength }}) </span>
        </template>

        <template v-slot:groupId="{ data: { item: { groupId } } }">
            <router-link
                v-if="groupId !== $mxs_t('not', { action: 'monitored' })"
                v-mxs-highlighter="{ keyword: search_keyword, txt: groupId }"
                :to="`/dashboard/monitors/${groupId}`"
                class="rsrc-link font-weight-bold"
            >
                {{ groupId }}
            </router-link>
            <span v-else v-mxs-highlighter="{ keyword: search_keyword, txt: groupId }">
                {{ groupId }}
            </span>
        </template>
        <template v-slot:groupId-append="{ data: { item: { groupId } } }">
            <span
                v-if="isCooperative(groupId)"
                class="ml-1 mxs-color-helper text-success cooperative-indicator"
            >
                Primary
            </span>
        </template>
        <template v-slot:monitorState="{ data: { item: { monitorState } } }">
            <div class="d-flex align-center">
                <icon-sprite-sheet
                    v-if="monitorState"
                    size="16"
                    class="monitor-state-icon mr-1"
                    :frame="$helpers.monitorStateIcon(monitorState)"
                >
                    monitors
                </icon-sprite-sheet>
                <span v-mxs-highlighter="{ keyword: search_keyword, txt: monitorState }">
                    {{ monitorState }}
                </span>
            </div>
        </template>

        <template
            v-slot:id="{
                    data: {
                        item: { id, isSlave, isMaster,  serverInfo = [] },
                    },
                }"
        >
            <rep-tooltip
                :disabled="!(isSlave || isMaster)"
                :serverInfo="serverInfo"
                :isMaster="isMaster"
                :open-delay="400"
                :top="true"
            >
                <template v-slot:activator="{ on }">
                    <div
                        :class="{
                            'override-td--padding disable-auto-truncate pointer text-truncate':
                                isSlave || isMaster,
                        }"
                        v-on="on"
                    >
                        <router-link
                            v-mxs-highlighter="{ keyword: search_keyword, txt: id }"
                            :to="`/dashboard/servers/${id}`"
                            class="rsrc-link"
                        >
                            {{ id }}
                        </router-link>
                    </div>
                </template>
            </rep-tooltip>
        </template>

        <template
            v-slot:serverState="{
                    data: {
                        item: {  serverState, isSlave, isMaster, serverInfo = [] },
                    },
                }"
        >
            <rep-tooltip
                v-if="serverState"
                :disabled="!(isSlave || isMaster)"
                :serverInfo="serverInfo"
                :isMaster="isMaster"
                :top="true"
            >
                <template v-slot:activator="{ on }">
                    <div
                        class="override-td--padding"
                        :class="{ pointer: isSlave || isMaster }"
                        v-on="on"
                    >
                        <icon-sprite-sheet
                            size="16"
                            class="mr-1 server-state-icon"
                            :frame="$helpers.serverStateIcon(serverState)"
                        >
                            servers
                        </icon-sprite-sheet>
                        <span v-mxs-highlighter="{ keyword: search_keyword, txt: serverState }">
                            {{ serverState }}
                        </span>
                    </div>
                </template>
            </rep-tooltip>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span
                v-if="typeof serviceIds === 'string'"
                v-mxs-highlighter="{ keyword: search_keyword, txt: serviceIds }"
            >
                {{ serviceIds }}
            </span>

            <template v-else-if="serviceIds.length < 2">
                <router-link
                    v-for="(serviceId, i) in serviceIds"
                    :key="i"
                    v-mxs-highlighter="{ keyword: search_keyword, txt: serviceId }"
                    :to="`/dashboard/services/${serviceId}`"
                    class="rsrc-link"
                >
                    {{ serviceId }}
                </router-link>
            </template>

            <v-menu
                v-else
                top
                offset-y
                transition="slide-y-transition"
                :close-on-content-click="false"
                open-on-hover
                allow-overflow
                content-class="shadow-drop"
            >
                <template v-slot:activator="{ on }">
                    <div
                        class="pointer mxs-color-helper text-anchor override-td--padding disable-auto-truncate"
                        v-on="on"
                    >
                        {{ serviceIds.length }}
                        {{ $mxs_tc('services', 2).toLowerCase() }}
                    </div>
                </template>

                <v-sheet class="pa-4">
                    <router-link
                        v-for="(serviceId, i) in serviceIds"
                        :key="i"
                        v-mxs-highlighter="{ keyword: search_keyword, txt: serviceId }"
                        :to="`/dashboard/services/${serviceId}`"
                        class="text-body-2 d-block rsrc-link"
                    >
                        {{ serviceId }}
                    </router-link>
                </v-sheet>
            </v-menu>
        </template>
    </data-table>
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
import { mapGetters, mapState } from 'vuex'
export default {
    data() {
        return {
            tableHeaders: [
                {
                    text: `Monitor`,
                    value: 'groupId',
                    autoTruncate: true,
                    padding: '0px 0px 0px 24px',
                },
                { text: 'State', value: 'monitorState', padding: '0px 12px 0px 24px' },
                { text: 'Servers', value: 'id', autoTruncate: true, padding: '0px 0px 0px 24px' },
                { text: 'Address', value: 'serverAddress', padding: '0px 0px 0px 24px' },
                {
                    text: 'Connections',
                    value: 'serverConnections',
                    autoTruncate: true,
                    padding: '0px 0px 0px 24px',
                },
                { text: 'State', value: 'serverState', padding: '0px 0px 0px 24px' },
                { text: 'GTID', value: 'gtid', padding: '0px 0px 0px 24px' },
                { text: 'Services', value: 'serviceIds', autoTruncate: true },
            ],
            servicesLength: 0,
            monitorsLength: 0,
            monitorSupportsReplica: 'mariadbmon',
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_servers: state => state.server.all_servers,
        }),
        ...mapGetters({
            getAllMonitorsMap: 'monitor/getAllMonitorsMap',
            getAllServersMap: 'server/getAllServersMap',
        }),
        tableRows: function() {
            let rows = []
            if (this.all_servers.length) {
                let allServiceIds = []
                let allMonitorIds = []
                let allMonitorsMapClone = this.$helpers.lodash.cloneDeep(this.getAllMonitorsMap)
                this.all_servers.forEach(server => {
                    const {
                        id,
                        attributes: {
                            state: serverState,
                            parameters: { address, port, socket },
                            statistics: { connections: serverConnections },
                            gtid_current_pos: gtid,
                        },
                        relationships: {
                            services: { data: servicesData = [] } = {},
                            monitors: { data: monitorsData = [] } = {},
                        },
                    } = server

                    const serviceIds = servicesData.length
                        ? servicesData.map(item => `${item.id}`)
                        : this.$mxs_t('noEntity', { entityName: 'services' })

                    if (typeof serviceIds !== 'string')
                        allServiceIds = [...allServiceIds, ...serviceIds]

                    let row = {
                        id,
                        serverAddress: socket ? socket : `${address}:${port}`,
                        serverConnections,
                        serverState,
                        serviceIds,
                        gtid,
                    }

                    if (this.getAllMonitorsMap.size && monitorsData.length) {
                        // The monitorsData is always an array with one element -> get monitor at index 0
                        const {
                            id: monitorId = null,
                            attributes: {
                                state: monitorState,
                                module: monitorModule,
                                monitor_diagnostics: { master: masterName, server_info = [] } = {},
                            },
                        } = this.getAllMonitorsMap.get(monitorsData[0].id) || {}

                        if (monitorId) {
                            allMonitorIds.push(monitorId)
                            row.groupId = monitorId
                            row.monitorState = monitorState
                            if (monitorModule === this.monitorSupportsReplica) {
                                if (masterName === row.id) {
                                    row.isMaster = true
                                    row.serverInfo = this.getAllSlaveServersInfo({
                                        masterName,
                                        server_info,
                                    })
                                } else {
                                    row.isSlave = true
                                    // get info of the server has name equal to row.id
                                    row.serverInfo = this.getSlaveServerInfo({
                                        masterName,
                                        slaveName: row.id,
                                        server_info,
                                    })
                                }
                            }
                            // delete monitor that already grouped from allMonitorsMapClone
                            allMonitorsMapClone.delete(monitorId)
                        }
                    } else {
                        row.groupId = this.$mxs_t('not', { action: 'monitored' })
                        row.monitorState = ''
                    }
                    rows.push(row)
                })

                // push monitors that don't monitor any servers to rows
                allMonitorsMapClone.forEach(monitor => {
                    allMonitorIds.push(monitor.id)
                    rows.push({
                        id: '',
                        serverAddress: '',
                        serverConnections: '',
                        serverState: '',
                        serviceIds: '',
                        gtid: '',
                        groupId: monitor.id,
                        monitorState: monitor.attributes.state,
                    })
                })

                const uniqueServiceId = new Set(allServiceIds) // get unique service ids
                this.setServicesLength([...uniqueServiceId].length)
                const uniqueMonitorId = new Set(allMonitorIds) // get unique monitor ids
                this.setMonitorsLength([...uniqueMonitorId].length)
            }
            return rows
        },
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
        setMonitorsLength(total) {
            this.monitorsLength = total
        },
        isCooperative(id) {
            return this.$typy(
                this.getAllMonitorsMap.get(id),
                'attributes.monitor_diagnostics.primary'
            ).safeBoolean
        },
        /**
         * Get info of the slave servers
         * @param {String} param.masterName - master server name
         * @param {Array} param.server_info - monitor_diagnostics.server_info
         * @returns {Array} returns all slave servers info of the provided masterName
         */
        getAllSlaveServersInfo({ masterName, server_info }) {
            return server_info.reduce((arr, item) => {
                if (item.name !== masterName)
                    arr.push({
                        ...item,
                        // Keep only connections to master
                        slave_connections: this.$helpers.filterSlaveConn({
                            slave_connections: item.slave_connections,
                            masterName,
                        }),
                    })
                return arr
            }, [])
        },
        /**
         * Get info of the slave servers
         * @param {String} param.masterName - master server name
         * @param {String} param.slaveName - slave server name
         * @param {Array} param.server_info - monitor_diagnostics.server_info
         * @returns {Array} All slave servers info of the provided masterName
         */
        getSlaveServerInfo({ masterName, slaveName, server_info }) {
            return server_info.reduce((arr, item) => {
                if (item.name === slaveName)
                    arr.push({
                        ...item,
                        slave_connections: this.$helpers.filterSlaveConn({
                            slave_connections: item.slave_connections,
                            masterName,
                        }),
                    })
                return arr
            }, [])
        },
    },
}
</script>

<style lang="scss" scoped>
.cooperative-indicator {
    font-size: 0.75rem;
}
</style>
