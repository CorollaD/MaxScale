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

import mount from '@tests/unit/setup'
import WkeNavTab from '../WkeNavTab.vue'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: WkeNavTab,
        propsData: {
            worksheet: dummyWke,
        },
        ...opts,
    })

const dummyWke = {
    id: '71cb4820-76d6-11ed-b6c2-dfe0423852da',
    query_editor_id: '71cb4821-76d6-11ed-b6c2-dfe0423852da',
    name: 'WORKSHEET',
}
describe('wke-nav-tab', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory({
            computed: {
                wkeId: () => dummyWke.id,
                isRunning: () => false,
            },
        })
    })

    it('Should show delete worksheet button', () => {
        wrapper = mountFactory()
        expect(wrapper.find('.del-tab-btn').exists()).to.be.equal(true)
    })

    it('Should show loading icon', () => {
        wrapper = mountFactory({ computed: { iRunning: () => true } })
        expect(wrapper.findComponent({ name: 'v-progress-circular' }.exists())).to.be.true
    })
})
