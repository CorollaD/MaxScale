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
import MxsCollapse from '@share/components/common/MxsCollapse'

describe('MxsCollapse.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: MxsCollapse,
            propsData: {
                isContentVisible: true,
                title: 'MxsCollapse title',
                toggleOnClick: () => {
                    // mockup isContentVisible reactivity props
                    wrapper.setProps({ isContentVisible: !wrapper.props().isContentVisible })
                },
            },
        })
    })

    it('Should mxs-collapse when toggle arrow is clicked', async () => {
        // this calls toggleOnClick cb which is handled in parent component
        await wrapper.find('.arrow-toggle').trigger('click')
        // component is collapsed when isContentVisible === false
        expect(wrapper.props().isContentVisible).to.be.false
    })

    it('Should display edit button when hover', async () => {
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            editable: true,
            onEdit: () => sinon.stub(),
        })
        wrapper.find('.mxs-collapse-wrapper').trigger('mouseenter')
        expect(wrapper.vm.$data.showEditBtn).to.be.true
    })

    it(`Should not display "add" button if onAddClick function props is null`, () => {
        expect(wrapper.find('.add-btn').exists()).to.be.equal(false)
    })

    it(`Should trigger onAddClick callback when onAddClick function props is passed and
      "add" button is clicked`, async () => {
        let eventFired = 0
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            onAddClick: () => {
                eventFired++
            },
        })
        wrapper.find('.add-btn').trigger('click')
        expect(eventFired).to.equal(1)
    })

    it(`Should not display "Done Editing" button when isEditing props is false`, () => {
        expect(wrapper.find('.done-editing-btn').exists()).to.be.equal(false)
    })
    it(`Should trigger doneEditingCb function props when the props is passed and
      "Done Editing" button is clicked`, async () => {
        let eventFired = 0
        // edit button is rendered only when onEdit props is passed with a function
        await wrapper.setProps({
            isEditing: true,
            doneEditingCb: () => {
                eventFired++

                wrapper.setProps({ isEditing: false })
            },
        })
        wrapper.find('.done-editing-btn').trigger('click')
        expect(eventFired).to.equal(1)
    })
})
