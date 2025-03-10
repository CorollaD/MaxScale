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
import mount, { router } from '@tests/unit/setup'
import ServerDetail from '@rootSrc/pages/ServerDetail'
import { dummy_all_servers, dummy_all_sessions, testRelationshipUpdate } from '@tests/unit/utils'

const dummy_monitor_diagnostics = {
    attributes: {
        monitor_diagnostics: {
            master: 'row_server_0',
            master_gtid_domain_id: 0,
            primary: null,
            server_info: [
                {
                    gtid_binlog_pos: '0-1000-9',
                    gtid_current_pos: '0-1000-9',
                    lock_held: null,
                    master_group: null,
                    name: 'row_server_0',
                    read_only: false,
                    server_id: 1000,
                    slave_connections: [],
                },
                {
                    gtid_binlog_pos: '0-1000-9',
                    gtid_current_pos: '0-1000-9',
                    lock_held: null,
                    master_group: null,
                    name: 'row_server_1',
                    read_only: false,
                    server_id: 1001,
                    slave_connections: [
                        {
                            connection_name: '',
                            gtid_io_pos: '0-1000-9',
                            last_io_error: '',
                            last_sql_error: '',
                            master_host: '127.0.0.1',
                            master_port: 4001,
                            master_server_id: 1000,
                            seconds_behind_master: 0,
                            slave_io_running: 'Yes',
                            slave_sql_running: 'Yes',
                        },
                    ],
                },
            ],
            state: 'Idle',
        },
    },
    id: 'Monitor',
    type: 'monitors',
}

const monitorDiagnosticsStub = {
    gtid_binlog_pos: '0-1000-9',
    gtid_current_pos: '0-1000-9',
    lock_held: null,
    master_group: null,
    name: 'row_server_0',
    read_only: false,
    server_id: 1000,
    slave_connections: [],
}

const EXPECT_SESSIONS_HEADER = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
    { text: 'Memory', value: 'memory' },
]

const toServerPage = async () => {
    const serverPath = `/dashboard/servers/${dummy_all_servers[0].id}`
    if (router.history.current.path !== serverPath) await router.push(serverPath)
}

const mountOptions = {
    shallow: false,
    component: ServerDetail,
    computed: {
        current_server: () => dummy_all_servers[0], // id: row_server_0
        monitor_diagnostics: () => dummy_monitor_diagnostics,
        filtered_sessions: () => dummy_all_sessions,
    },
    stubs: {
        'refresh-rate': "<div class='refresh-rate'></div>",
    },
}

describe('ServerDetail index', () => {
    let wrapper, axiosGetStub, axiosPatchStub

    before(async () => {
        await toServerPage()
        wrapper = mount(mountOptions)
        axiosGetStub = sinon.stub(wrapper.vm.$http, 'get').returns(Promise.resolve({ data: {} }))
        axiosPatchStub = sinon.stub(wrapper.vm.$http, 'patch').returns(Promise.resolve())
    })

    after(async () => {
        await axiosGetStub.restore()
        await axiosPatchStub.restore()
        await wrapper.destroy()
    })

    it(`Should send request to get current server, relationships
      services state then all sessions if current active tab is
       'Statistics & Sessions'`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,

                relationships: {
                    services: { data: servicesData },
                },
            } = dummy_all_servers[0]

            await axiosGetStub.should.have.been.calledWith(`/servers/${id}`)
            let count = 1
            await servicesData.forEach(async service => {
                await axiosGetStub.should.have.been.calledWith(
                    `/services/${service.id}?fields[services]=state`
                )
                ++count
            })
            await axiosGetStub.should.have.been.calledWith(`/sessions`)
            ++count
            axiosGetStub.should.have.callCount(count)
        })
    })

    it(`Should send GET requests to get server module parameters and
       monitor diagnostics if current active tab is 'Parameters & Diagnostics tab'`, async () => {
        await wrapper.setData({
            currentActiveTab: 1,
        })
        const monitorId = dummy_all_servers[0].relationships.monitors.data[0].id
        await axiosGetStub.should.have.been.calledWith(
            `/monitors/${monitorId}?fields[monitors]=monitor_diagnostics`
        )
        await axiosGetStub.should.have.been.calledWith(
            `/maxscale/modules/servers?fields[modules]=parameters`
        )
    })

    it(`Should send PATCH request with accurate payload to
      update services relationship`, async () => {
        const serviceTableRowProcessingSpy = sinon.spy(wrapper.vm, 'serviceTableRowProcessing')
        const relationshipType = 'services'
        await testRelationshipUpdate({
            wrapper,
            currentResource: dummy_all_servers[0],
            axiosPatchStub,
            relationshipType,
        })
        await axiosGetStub.should.have.been.calledWith(`/servers/${dummy_all_servers[0].id}`)
        // callback after update
        await serviceTableRowProcessingSpy.should.have.been.calledOnce
    })

    it(`Should send PATCH request with accurate payload to
      update monitors relationship`, async () => {
        const fetchMonitorDiagnosticsSpy = sinon.spy(wrapper.vm, 'fetchMonitorDiagnostics')
        const relationshipType = 'monitors'
        await testRelationshipUpdate({
            wrapper,
            currentResource: dummy_all_servers[0],
            axiosPatchStub,
            relationshipType,
        })
        await axiosGetStub.should.have.been.calledWith(`/servers/${dummy_all_servers[0].id}`)
        // callback after update
        await fetchMonitorDiagnosticsSpy.should.have.been.calledOnce
    })

    it(`Should pass necessary props value to 'STATISTICS' table`, () => {
        const statsTable = wrapper.findComponent({ ref: 'statistics-table' })
        expect(statsTable.exists()).to.be.true
        const { title, tableData, isTree } = statsTable.vm.$props
        expect(title).to.be.equals('statistics')
        expect(tableData).to.be.deep.equals(wrapper.vm.serverStats)
        expect(isTree).to.be.true
    })

    it(`Should pass necessary props value to 'CURRENT SESSIONS' table`, () => {
        const sessionsTable = wrapper.findComponent({ name: 'sessions-table' })
        expect(sessionsTable.exists()).to.be.true
        const { search, sortDesc, sortBy } = sessionsTable.vm.$attrs
        const { collapsible, delayLoading, rows } = sessionsTable.vm.$props
        const { search_keyword, sessionsTableRow } = wrapper.vm
        expect(search).to.be.equals(search_keyword)
        expect(rows).to.be.eql(sessionsTableRow)
        expect(collapsible).to.be.true
        expect(delayLoading).to.be.true
        expect(sortDesc).to.be.true
        expect(sortBy).to.be.equals('connected')
    })

    it(`Should use accurate table headers for 'CURRENT SESSIONS' table`, () => {
        const sessionsTable = wrapper.findComponent({ name: 'sessions-table' })
        expect(sessionsTable.vm.$props.headers).to.be.deep.equals(EXPECT_SESSIONS_HEADER)
    })

    it(`Should compute sessions for this server to accurate data format`, () => {
        expect(wrapper.vm.sessionsTableRow[0]).to.include.all.keys(
            'id',
            'user',
            'connected',
            'idle',
            'memory'
        )
        expect(wrapper.vm.sessionsTableRow[0].memory).to.be.an('object')
    })

    it(`Should pass necessary props value to 'MONITOR DIAGNOSTICS' table`, () => {
        const diagnosticsTable = wrapper.findComponent({ ref: 'diagnostics-table' })
        expect(diagnosticsTable.exists()).to.be.true
        const { title, tableData, isTree, expandAll } = diagnosticsTable.vm.$props

        expect(title).to.be.equals('Monitor Diagnostics')
        expect(isTree).to.be.true
        expect(expandAll).to.be.true
        expect(tableData).to.be.deep.equals(wrapper.vm.monitorDiagnostics)
    })

    it(`Should compute monitor diagnostics for this server to accurate data format`, () => {
        expect(wrapper.vm.monitorDiagnostics).to.be.deep.equals(monitorDiagnosticsStub)
    })

    it(`Should pass necessary props value to 'SERVICES' table`, () => {
        const servicesTable = wrapper.findComponent({ name: 'relationship-table' })
        expect(servicesTable.exists()).to.be.true
        const {
            relationshipType,
            addable,
            removable,
            tableRows,
            getRelationshipData,
        } = servicesTable.vm.$props

        const {
            $data: { serviceTableRow },
            getRelationshipData: getRelationshipDataAsync,
        } = wrapper.vm

        expect(relationshipType).to.be.equals('services')
        expect(addable).to.be.true
        expect(removable).to.be.true
        expect(tableRows).to.be.deep.equals(serviceTableRow)
        expect(getRelationshipData).to.be.equals(getRelationshipDataAsync)
    })

    it(`Should compute serviceTableRow for this server to accurate data format`, async () => {
        let getRelationshipDataStub

        getRelationshipDataStub = sinon.stub(wrapper.vm, 'getRelationshipData')
        getRelationshipDataStub.onCall(0).returns(
            Promise.resolve({
                attributes: {
                    state: 'Started',
                },
                id: 'service_0',
                type: 'services',
            })
        )

        const serviceTableRowStub = [
            {
                id: 'service_0',
                state: 'Started',
                type: 'services',
            },
        ]
        await wrapper.vm.serviceTableRowProcessing()
        expect(wrapper.vm.$data.serviceTableRow).to.be.deep.equals(serviceTableRowStub)
    })

    it(`Should pass necessary props value to 'PARAMETERS' table`, () => {
        const paramsTable = wrapper.findComponent({ name: 'details-parameters-table' })
        expect(paramsTable.exists()).to.be.true
        const {
            resourceId,
            parameters,
            usePortOrSocket,
            updateResourceParameters,
            onEditSucceeded,
        } = paramsTable.vm.$props

        const { updateServerParameters, dispatchFetchServer } = wrapper.vm
        const {
            id: serverId,
            attributes: { parameters: serverParams },
        } = dummy_all_servers[0]

        expect(resourceId).to.be.equals(serverId)
        expect(parameters).to.be.deep.equals(serverParams)
        expect(usePortOrSocket).to.be.true
        expect(updateResourceParameters).to.be.equals(updateServerParameters)
        expect(onEditSucceeded).to.be.equals(dispatchFetchServer)
    })
})
