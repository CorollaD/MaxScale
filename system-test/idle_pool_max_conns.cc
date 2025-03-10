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

#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxbase/format.hh>
#include "maxbase/stopwatch.hh"

using std::string;
using std::move;

void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    mxs.check_servers_status(mxt::ServersInfo::default_repl_states());

    const string basic_uname = "basic";
    const string basic_pass = "cisab";
    const int pooling_time = 1;

    auto* admin_conn = repl.backend(0)->admin_connection();
    auto basic_user = admin_conn->create_user(basic_uname, "%", basic_pass);

    // Need a separate connection to each server.
    std::vector<std::unique_ptr<mxt::MariaDB>> backend_conns;
    for (int i = 0; i < repl.N; i++)
    {
        auto conn = repl.backend(i)->open_connection();
        backend_conns.push_back(move(conn));
    }

    if (test.ok())
    {
        std::atomic_bool keep_running {true};
        const int max_expected_conns = 100;

        auto check_conn_counts = [&]() {
                const string user_count_query = mxb::string_printf(
                    "select count(*) from information_schema.processlist where user = '%s';",
                    basic_uname.c_str());
                int check_iter = 0;

                int64_t counts[backend_conns.size()];

                while (keep_running)
                {
                    for (size_t i = 0; i < backend_conns.size(); i++)
                    {
                        auto& conn = backend_conns[i];
                        auto res = conn->query(user_count_query);
                        if (res && res->next_row())
                        {
                            auto count = res->get_int(0);
                            counts[i] = count;

                            test.expect(count <= max_expected_conns,
                                        "Connection count of '%s' is %li, when a maximum of %i was expected.",
                                        repl.backend(i)->cnf_name().c_str(), count, max_expected_conns);
                        }
                        else
                        {
                            counts[i] = -1;
                            test.add_failure("Failed to get connection count from '%s'.",
                                             repl.backend(i)->cnf_name().c_str());
                        }
                    }

                    // Every few iterations, print the connection counts.
                    if (check_iter % 3 == 0)
                    {
                        string msg = "Connection counts for servers:\n";
                        for (size_t i = 0; i < backend_conns.size(); i++)
                        {
                            msg += mxb::string_printf("%s: %li\t", repl.backend(i)->cnf_name().c_str(),
                                                      counts[i]);
                        }
                        test.tprintf("%s", msg.c_str());
                    }
                    check_iter++;
                    sleep(1);
                }
            };

        // Backend servers should only have max 100 connections at any time. Start a separate thread
        // which checks the number continuously.
        std::thread conn_count_check_thread(check_conn_counts);

        // Make 900 sessions. No backend should have more than ~100 connections at any given time.
        const int n_sessions = 900;
        const string basic_q = "select rand();";
        std::vector<std::unique_ptr<mxt::MariaDB>> sessions;

        for (int i = 0; i < n_sessions && test.ok(); i++)
        {
            auto conn = mxs.try_open_connection(4006, basic_uname, basic_pass);
            if (conn->is_open())
            {
                auto res = conn->try_query(basic_q);
                if (res && res->next_row())
                {
                    sessions.push_back(move(conn));
                }
                else
                {
                    test.add_failure("Query to connection %i failed.", i);
                }
            }
            else
            {
                test.add_failure("Connection %i failed.", i);
            }
        }

        if (test.ok())
        {
            test.tprintf("%i sessions created and queried.", n_sessions);
            sleep(pooling_time);

            // Query sessions in batches such that wait time is limited within a batch.
            auto begin_ind = 0;
            const int simult_sessions = 3 * max_expected_conns; // 3 slaves, assume they are used evenly.

            while (begin_ind < n_sessions)
            {
                int end_ind = std::min(begin_ind + simult_sessions, n_sessions);

                test.tprintf("Query sessions %i -- %i. This should be fast and not require much waiting.",
                             begin_ind + 1, end_ind);

                mxb::StopWatch timer;
                for (int i = begin_ind; i < end_ind; i++)
                {
                    auto res = sessions[i]->query(basic_q);
                    test.expect(res && res->next_row(), "Query failed or returned no data.");
                }
                auto time_spent = timer.lap();
                auto time_spent_s = mxb::to_secs(time_spent);
                auto time_limit = 4s;
                if (time_spent <= time_limit)   // Usually closer to 2s but need to have some slack.
                {
                    test.tprintf("Querying took %f seconds.", time_spent_s);
                }
                else
                {
                    test.add_failure("Querying took %f seconds when %ld or less was expected.",
                                     time_spent_s, time_limit.count());
                }

                begin_ind = end_ind;
                sleep(pooling_time);
            }

            if (test.ok())
            {
                test.tprintf("Query all sessions. This can take a few seconds.");
                mxb::StopWatch timer;
                for (int i = 0; i < n_sessions; i++)
                {
                    auto res = sessions[i]->query(basic_q);
                    test.expect(res && res->next_row(), "Query failed or returned no data.");
                }
                auto time_spent = timer.lap();
                auto time_spent_s = mxb::to_secs(time_spent);

                if (time_spent <= 6s)
                {
                    test.tprintf("Querying took %f seconds.", time_spent_s);
                }
                else
                {
                    test.add_failure("Querying took %f seconds when 6 or less was expected.", time_spent_s);
                }
            }
        }
        keep_running = false;
        conn_count_check_thread.join();
    }
}
