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
import UidInput from '../UidInput.vue'
import { getErrMsgEle, inputChangeMock } from '@tests/unit/utils'

describe(`uid-input - form input tests`, () => {
    let wrapper

    it(`Should show error message if userID value is empty`, async () => {
        wrapper = mount({ shallow: false, component: UidInput, attrs: { value: 'maxskysql' } })
        const inputComponent = wrapper
        await inputChangeMock(inputComponent, '')
        expect(getErrMsgEle(inputComponent).text()).to.be.equals(
            wrapper.vm.$mxs_t('errors.requiredInput', { inputName: wrapper.vm.$mxs_t('username') })
        )
    })
})
