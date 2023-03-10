/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/testparser.hh>
#include <sstream>

using namespace std;

namespace
{

mxs::Parser::Plugin* load_parser(const string& plugin,
                                 mxs::Parser::SqlMode sql_mode,
                                 const string& plugin_args)
{
    mxs::Parser::Plugin* pPlugin = mxs::Parser::load(plugin.c_str());

    if (!pPlugin)
    {
        ostringstream ss;
        ss << "Could not load parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (!pPlugin->setup(sql_mode, plugin_args.c_str()))
    {
        mxs::Parser::unload(pPlugin);
        ostringstream ss;

        ss << "Could not setup parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    if (!pPlugin->thread_init())
    {
        mxs::Parser::unload(pPlugin);
        ostringstream ss;

        ss << "Could not perform thread initialization for parser plugin '" << plugin << "'.";

        throw std::runtime_error(ss.str());
    }

    mxs::CachingParser::thread_init();

    return pPlugin;
}

}

namespace maxscale
{

TestParser::TestParser(const string& plugin, SqlMode sql_mode, const string& plugin_args)
    : CachingParser(load_parser(plugin, sql_mode, plugin_args))
{
}

TestParser::~TestParser()
{
    m_plugin.thread_end();
    mxs::CachingParser::thread_finish();
}

}
