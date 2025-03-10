<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                {{ $mxs_t('selectObjsToMigrate') }}
            </h3>
        </template>
        <template v-slot:body>
            <v-row class="fill-height">
                <v-col cols="12" md="6" class="fill-height">
                    <div class="d-flex flex-column fill-height">
                        <etl-create-mode-input :taskId="task.id" class="mb-2" />
                        <mxs-treeview
                            ref="tree"
                            v-model="selectedObjs"
                            class="mxs-treeview--src-treeview fill-height overflow-y-auto mxs-color-helper all-border-separator pa-2 rounded"
                            :items="srcSchemaTree"
                            hoverable
                            dense
                            open-on-click
                            transition
                            selectable
                            :load-children="handleLoadChildren"
                            return-object
                        >
                            <template v-slot:label="{ item: node }">
                                <div class="d-flex align-center">
                                    <v-icon
                                        size="18"
                                        color="blue-azure"
                                        :class="{ 'ml-1': iconSheet(node) }"
                                    >
                                        {{ iconSheet(node) }}
                                    </v-icon>
                                    <span class="ml-1 text-truncate d-inline-block node-name">
                                        {{ node.name }}
                                    </span>
                                </div>
                            </template>
                        </mxs-treeview>
                    </div>
                </v-col>
                <v-col cols="12" md="6" class="fill-height">
                    <etl-logs :task="task" class="fill-height" :class="{ 'pt-10': isLarge }" />
                </v-col>
            </v-row>
        </template>
        <template v-slot:footer>
            <div class="etl-obj-select-stage-footer d-flex flex-column justify-end">
                <p v-if="errMsg" class="v-messages__message error--text">
                    {{ errMsg }}
                </p>
                <p v-else-if="infoMsg" class="v-messages__message warning--text">
                    {{ infoMsg }}
                </p>
                <v-checkbox
                    v-if="showConfirm"
                    v-model="isConfirmed"
                    color="primary"
                    class="mt-0 mb-4 v-checkbox--mariadb"
                    hide-details
                >
                    <template v-slot:label>
                        <v-tooltip top transition="slide-y-transition" max-width="340">
                            <template v-slot:activator="{ on }">
                                <div class="d-flex align-center" v-on="on">
                                    <label
                                        class="v-label ml-1 mxs-color-helper text-deep-ocean confirm-label"
                                    >
                                        {{ $mxs_t('etlConfirmMigration') }}
                                    </label>
                                    <v-icon
                                        class="ml-1 material-icons-outlined pointer"
                                        size="16"
                                        color="warning"
                                    >
                                        $vuetify.icons.mxs_statusWarning
                                    </v-icon>
                                </div>
                            </template>
                            <span>{{ $mxs_t('info.etlConfirm') }}</span>
                        </v-tooltip>
                    </template>
                </v-checkbox>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="font-weight-medium px-7 text-capitalize prepare-btn"
                    rounded
                    depressed
                    :disabled="disabled"
                    @click="next"
                >
                    {{ $mxs_t('prepareMigrationScript') }}
                </v-btn>
            </div>
        </template>
    </etl-stage-ctr>
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
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsSrc/store/orm/models/EtlTaskTmp'
import { mapState } from 'vuex'
import queryHelper from '@wsSrc/store/queryHelper'
import EtlStageCtr from '@wkeComps/DataMigration/EtlStageCtr.vue'
import EtlCreateModeInput from '@wkeComps/DataMigration/EtlCreateModeInput.vue'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs.vue'

export default {
    name: 'etl-obj-select-stage',
    components: {
        EtlStageCtr,
        EtlLogs,
        EtlCreateModeInput,
    },
    props: { task: { type: Object, required: true } },
    data() {
        return {
            selectedObjs: [],
            errMsg: '',
            infoMsg: '',
            isLarge: true,
            isConfirmed: false,
        }
    },
    computed: {
        ...mapState({
            ETL_CREATE_MODES: state => state.mxsWorkspace.config.ETL_CREATE_MODES,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            NODE_GROUP_TYPES: state => state.mxsWorkspace.config.NODE_GROUP_TYPES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
        }),
        srcSchemaTree() {
            return EtlTask.getters('getSrcSchemaTree')(this.task.id)
        },
        createMode() {
            return EtlTask.getters('getCreateMode')(this.task.id)
        },
        parsedObjs() {
            return this.selectedObjs.reduce(
                (obj, o) => {
                    const schema = queryHelper.getSchemaName(o)
                    // TBL_G nodes will be included in selectedObjs if those have no tables
                    if (o.type === this.NODE_GROUP_TYPES.TBL_G) obj.emptySchemas.push(schema)
                    else obj.tables.push({ schema, table: o.name })
                    return obj
                },
                { tables: [], emptySchemas: [] }
            )
        },
        tables() {
            return this.parsedObjs.tables
        },
        disabled() {
            if (this.tables.length) return this.showConfirm ? !this.isConfirmed : false
            return !this.tables.length
        },
        showConfirm() {
            return this.createMode === this.ETL_CREATE_MODES.REPLACE
        },
        isActive() {
            return this.task.active_stage_index === this.ETL_STAGE_INDEX.SRC_OBJ
        },
    },
    watch: {
        selectedObjs: {
            deep: true,
            handler(v) {
                if (v.length) {
                    if (this.tables.length) {
                        this.errMsg = ''
                        this.infoMsg = this.parsedObjs.emptySchemas.length
                            ? this.$mxs_t('info.ignoreSchemas')
                            : ''
                    } else this.errMsg = this.$mxs_t('errors.invalidChosenSchemas')
                } else this.errMsg = this.$mxs_t('errors.emptyMigrationObj')

                EtlTaskTmp.update({ where: this.task.id, data: { migration_objs: this.tables } })
            },
        },
        '$vuetify.breakpoint.width': {
            immediate: true,
            handler(v) {
                this.isLarge = v >= 960
            },
        },
    },

    async activated() {
        if (this.isActive) await EtlTask.dispatch('fetchSrcSchemas')
    },
    methods: {
        filter(node, search, textKey) {
            return this.$helpers.ciStrIncludes(node[textKey], search)
        },
        iconSheet(node) {
            const { SCHEMA } = this.NODE_TYPES
            const { TBL_G } = this.NODE_GROUP_TYPES
            switch (node.type) {
                case SCHEMA:
                    return '$vuetify.icons.mxs_database'
                case TBL_G:
                    return '$vuetify.icons.mxs_table'
            }
        },
        /**
         *
         * @param {Object} param.node - node to have group node
         * @param {Object} param.groupNode - group node to be added
         */
        addGroupNode({ node, groupNode }) {
            const tree = queryHelper.deepReplaceNode({
                treeData: this.srcSchemaTree,
                node: { ...node, children: [groupNode] },
            })
            EtlTaskTmp.update({
                where: this.task.id,
                data: { src_schema_tree: tree },
            })
        },
        /**
         * For now, only TBL nodes can be migrated, so when expanding a SCHEMA node
         * the TBL_G will be automatically added and expanded to improves UX.
         * If sproc, functions or views are later supported, this won't be needed
         */
        async handleLoadChildren(node) {
            if (node.type === this.NODE_TYPES.SCHEMA) {
                const tblGroupNode = queryHelper.genNodeGroup({
                    parentNode: node,
                    type: this.NODE_GROUP_TYPES.TBL_G,
                })
                this.addGroupNode({ node, groupNode: tblGroupNode })
                await EtlTask.dispatch('loadChildNodes', tblGroupNode)
                // expand TBL_G node automatically after fetching its child
                this.$refs.tree.updateOpen(tblGroupNode.id, true)
            }
        },

        async next() {
            EtlTask.update({
                where: this.task.id,
                data(obj) {
                    obj.active_stage_index = obj.active_stage_index + 1
                    obj.is_prepare_etl = true
                },
            })
            await EtlTask.dispatch('handleEtlCall', {
                id: this.task.id,
                tables: EtlTask.getters('getMigrationObjs')(this.task.id),
            })
        },
    },
}
</script>

<style lang="scss">
.etl-obj-select-stage-footer {
    .prepare-btn {
        width: 215px;
    }
    .confirm-label {
        font-size: 0.875rem;
    }
}
.mxs-treeview--src-treeview {
    font-size: 0.875rem;
    background-color: #fbfbfb;
    .v-treeview-node__content {
        margin-left: 0px;
    }
}
</style>
