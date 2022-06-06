/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>

namespace
{
// watchdog_interval 60 seconds, make sure it is the same in maxscale.service
const maxbase::Duration watchdog_interval = mxb::from_secs(60.0);

// Return true if maxscale stays alive for the duration dur.
bool staying_alive(TestConnections& test, const maxbase::Duration& dur)
{
    bool alive = true;
    maxbase::StopWatch sw_loop_start;
    while (alive && sw_loop_start.split() < dur)
    {
        if (execute_query_silent(test.maxscale->conn_rwsplit, "select 1"))
        {
            alive = false;
            break;
        }
    }

    return alive;
}

// The bulk of the test.
void test_watchdog(TestConnections& test, int argc, char* argv[])
{
    test.log_includes("The systemd watchdog is Enabled");

    // Wait for one watchdog interval, systemd should have been notified in that time.
    staying_alive(test, watchdog_interval);

    test.log_includes("systemd watchdog keep-alive ping");

    test.reset_timeout();

    // Make one thread in maxscale hang
    mysql_query(test.maxscale->conn_rwsplit, "select LUA_INFINITE_LOOP");

    // maxscale should get killed by systemd in less than duration(interval - epsilon).
    bool maxscale_alive = staying_alive(test, mxb::from_secs(1.2 * mxb::to_secs(watchdog_interval)));

    if (maxscale_alive)
    {
        test.add_result(true, "Although the systemd watchdog is enabled, "
                              "systemd did not terminate maxscale!");
    }
    else
    {
        test.log_includes("received fatal signal 6");
        if (test.global_result == 0)
        {
            test.tprintf("Maxscale was killed by systemd - ok");

            bool core_removed = false;

            for (int i = 0; i < 30 && !core_removed; i++)
            {
                if (test.maxscale->ssh_output("rm /tmp/core*", true).rc == 0)
                {
                    core_removed = true;
                }
            }

            test.expect(core_removed, "Removing core files should work:");
        }
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test {argc, argv};

    std::string lua_file("/infinite_loop.lua");
    std::string from = mxt::SOURCE_DIR + lua_file;
    std::string to = test.maxscale->access_homedir() + lua_file;

    test.maxscale->copy_to_node(from.c_str(), to.c_str());
    test.maxscale->ssh_node((std::string("chmod a+r ") + to).c_str(), true);
    test.maxscale->start();
    sleep(2);
    test.maxscale->wait_for_monitor();
    test.maxscale->connect_rwsplit();

    if (!test.global_result)
    {
        test_watchdog(test, argc, argv);
    }

    return test.global_result;
}
