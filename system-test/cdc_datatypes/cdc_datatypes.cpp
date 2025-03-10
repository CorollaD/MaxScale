/**
 * @file cdc_connect.cpp Test the CDC protocol
 */

#include <maxtest/testconnections.hh>
#include <maxtest/generate_sql.hh>
#include <cdc_connector.h>
#include "cdc_result.h"
#include <iostream>
#include <stdio.h>

static std::string unquote(std::string str)
{
    if (str[0] == '\"' || str[0] == '\'')
    {
        str = str.substr(1, str.length() - 2);
    }

    return str;
}

bool run_test(TestConnections& test)
{
    bool rval = true;

    test.repl->connect();
    execute_query(test.repl->nodes[0], "RESET MASTER");
    test.repl->close_connections();
    test.maxscale->start();

    std::set<std::string> excluded = {"JSON", "INET6"};
    std::vector<sql_generation::SQLType> test_set;

    for (const auto& t : sql_generation::mariadb_types())
    {
        if (excluded.count(t.type_name) == 0)
        {
            test_set.push_back(t);
        }
    }

    test.tprintf("Inserting data");
    for (const auto& t : test_set)
    {
        test.repl->connect();
        execute_query(test.repl->nodes[0], "%s", t.create_sql.c_str());

        for (const auto& v : t.values)
        {
            execute_query(test.repl->nodes[0], "%s", v.insert_sql.c_str());
        }

        execute_query(test.repl->nodes[0], "%s", t.drop_sql.c_str());
        test.repl->close_connections();
    }

    test.tprintf("Waiting for avrorouter to process data");
    test.repl->connect();
    execute_query(test.repl->nodes[0], "FLUSH LOGS");
    test.repl->close_connections();
    sleep(10);

    for (const auto& t : test_set)
    {
        test.reset_timeout();
        test.tprintf("Testing type: %s", t.type_name.c_str());
        CDC::Connection conn(test.maxscale->ip4(), 4001, "skysql", "skysql");

        if (conn.connect(t.full_name))
        {
            for (const auto& v : t.values)
            {
                CDC::SRow row;

                if ((row = conn.read()))
                {
                    std::string input = unquote(v.value);
                    std::string output = row->value(t.field_name);

                    if (input == output || (input == "NULL" && (output == "" || output == "0")))
                    {
                        // Expected result
                    }
                    else
                    {
                        test.tprintf("Result mismatch: %s(%s) => %s",
                                     t.type_name.c_str(),
                                     input.c_str(),
                                     output.c_str());
                        rval = false;
                    }
                }
                else
                {
                    std::string err = conn.error();
                    test.tprintf("Failed to read data: %s", err.c_str());
                }
            }
        }
        else
        {
            std::string err = conn.error();
            test.tprintf("Failed to request data: %s", err.c_str());
            rval = false;
            break;
        }
    }

    return rval;
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    if (!run_test(test))
    {
        test.add_result(1, "Test failed");
    }

    test.maxscale->expect_running_status(true);

    return test.global_result;
}
