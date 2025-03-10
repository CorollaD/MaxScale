import Vue from 'vue'
import Vuetify from 'vuetify/lib'
import helpersPlugin from '@share/plugins/helpers'
import logger from '@share/plugins/logger'
import typy from '@share/plugins/typy'
import shortkey from '@share/plugins/shortkey'
import scopingI18n from '@share/plugins/scopingI18n'
import txtHighlighter from '@share/plugins/txtHighlighter'
import * as maxguiHelpers from '@rootSrc/utils/helpers'
import * as workspaceHelpers from '@wsSrc/utils/helpers'
import Vuex from 'vuex'
import PortalVue from 'portal-vue'
import VueMoment from 'vue-moment'
import momentDurationFormatSetup from 'moment-duration-format'
import VueI18n from 'vue-i18n'
import Workspace from '@rootSrc/plugins/workspace.js'

Vue.use(VueI18n)
// i18n only available after Vue.use(VueI18n)
Vue.use(scopingI18n, { i18n: require('@share/plugins/i18n').default })
Vue.use(Vuetify)
Vue.use(helpersPlugin, { addon: { ...maxguiHelpers, ...workspaceHelpers } })
Vue.use(typy)
Vue.use(shortkey)
Vue.use(logger)
Vue.use(txtHighlighter)
Vue.use(Vuex)
// portal-value isn't needed in test env
if (process.env.NODE_ENV !== 'test') Vue.use(PortalVue)
Vue.use(VueMoment)
momentDurationFormatSetup(Vue.moment)
// store only available after Vue.use(Vuex), so here use require for importing es module
const store = require('./store/index').default
Vue.use(require('@share/plugins/http').default, { store })
Vue.use(Workspace, { store })
