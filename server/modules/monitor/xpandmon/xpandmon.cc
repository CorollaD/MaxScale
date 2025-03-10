/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "xpandmon.hh"
#include <maxscale/modinfo.hh>
#include <maxscale/modulecmd.hh>
#include "xpandmonitor.hh"

namespace
{

bool handle_softfail(const MODULECMD_ARG* args, json_t** error_out)
{
    mxb_assert(args->argc == 2);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    XpandMonitor* pMon = static_cast<XpandMonitor*>(args->argv[0].value.monitor);
    SERVER* pServer = args->argv[1].value.server;

    return pMon->softfail(pServer, error_out);
}

bool handle_unsoftfail(const MODULECMD_ARG* args, json_t** error_out)
{
    mxb_assert(args->argc == 2);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&args->argv[1].type) == MODULECMD_ARG_SERVER);

    XpandMonitor* pMon = static_cast<XpandMonitor*>(args->argv[0].value.monitor);
    SERVER* pServer = args->argv[1].value.server;

    return pMon->unsoftfail(pServer, error_out);
}
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t softfail_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Monitor name (from configuration file)"
        },
        {
            MODULECMD_ARG_SERVER, "Node to be softfailed."
        }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "softfail", MODULECMD_TYPE_ACTIVE,
                               handle_softfail, MXS_ARRAY_NELEMS(softfail_argv), softfail_argv,
                               "Perform softfail of node");

    static modulecmd_arg_type_t unsoftfail_argv[] =
    {
        {
            MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Monitor name (from configuration file)"
        },
        {
            MODULECMD_ARG_SERVER, "Node to be unsoftfailed."
        }
    };

    modulecmd_register_command(MXB_MODULE_NAME, "unsoftfail", MODULECMD_TYPE_ACTIVE,
                               handle_unsoftfail, MXS_ARRAY_NELEMS(unsoftfail_argv), unsoftfail_argv,
                               "Perform unsoftfail of node");

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::MONITOR,
        mxs::ModuleStatus::GA,
        MXS_MONITOR_VERSION,
        "A Xpand cluster monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<XpandMonitor>::s_api,
        NULL,                                       /* Process init. */
        NULL,                                       /* Process finish. */
        NULL,                                       /* Thread init. */
        NULL,                                       /* Thread finish. */
        XpandMonitor::specification()
    };

    return &info;
}
