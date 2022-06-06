/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import Sessions from '@/pages/Dashboard/Sessions'

import {
    dummy_all_sessions,
    findAnchorLinkInTable,
    getUniqueResourceNamesStub,
} from '@tests/unit/utils'

const expectedTableHeaders = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
    { text: 'Service', value: 'serviceIds' },
]

const expectedTableRows = [
    {
        id: '1000002',
        user: 'maxskysql@::ffff:127.0.0.1',
        connected: 'Thu Aug 13 14:06:17 2020',
        idle: 55.5,
        serviceIds: ['RCR-Router'],
    },
]

describe('Dashboard Sessions tab', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: Sessions,
            computed: {
                all_sessions: () => dummy_all_sessions,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$store.$http, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should process table rows accurately`, () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const sessionId = dummy_all_sessions[0].id
        const serviceId = dummy_all_sessions[0].relationships.services.data[0].id
        const aTag = findAnchorLinkInTable({
            wrapper: wrapper,
            rowId: sessionId,
            cellIndex: expectedTableHeaders.findIndex(item => item.value === 'serviceIds'),
        })
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should get total number of unique service names accurately`, () => {
        const uniqueServiceNames = getUniqueResourceNamesStub(expectedTableRows, 'serviceIds')
        expect(wrapper.vm.$data.servicesLength).to.be.equals(uniqueServiceNames.length)
    })
})
