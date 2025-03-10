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
import WkeToolbar from '../WkeToolbar.vue'

const mountFactory = opts => mount({ shallow: false, component: WkeToolbar, ...opts })

describe(`wke-toolbar - mounted hook and child component's interaction tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-btn-width evt', () => {
        expect(wrapper.emitted()).to.have.property('get-total-btn-width')
    })
    it(`Should call add`, async () => {
        let isCalled = false
        let wrapper = mountFactory({
            computed: { isAddWkeDisabled: () => false },
            methods: { add: () => (isCalled = true) },
        })
        await wrapper.find('.add-wke-btn').trigger('click')
        expect(isCalled).to.be.true
    })

    it('Should pass accurate data to query-cnf-dlg-ctr via attrs', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-cnf-dlg-ctr' })
        expect(cnfDlg.vm.$attrs.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
    it(`Should popup query setting dialog`, () => {
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })

    it(`Should call SET_IS_FULLSCREEN mutation`, () => {
        let wrapper = mountFactory()
        const spy = sinon.spy(wrapper.vm, 'SET_IS_FULLSCREEN')
        const btn = wrapper.find('.min-max-btn')
        btn.trigger('click')
        spy.should.have.been.calledOnce
    })
})
