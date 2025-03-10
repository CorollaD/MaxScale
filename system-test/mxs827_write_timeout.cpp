/**
 * @file mxs827_write_timeout "ReadWriteSplit only keeps used connection alive, query crashes after unused
 * connection times out"
 * - SET wait_timeout=20
 * - do only SELECT during 30 seconds
 * - try INSERT
 */

#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>
#include <maxtest/sql_t1.hh>

using namespace std;

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();
    Test->maxscale->connect_maxscale();

    Test->try_query(Test->maxscale->conn_rwsplit, "SET wait_timeout=20");

    create_t1(Test->maxscale->conn_rwsplit);

    Test->tprintf("Doing reads for 30 seconds");
    time_t start = time(NULL);

    while (time(NULL) - start < 30 && Test->ok())
    {
        Test->reset_timeout();
        Test->try_query(Test->maxscale->conn_rwsplit, "SELECT 1");
    }

    Test->tprintf("Doing one write");
    Test->try_query(Test->maxscale->conn_rwsplit, "INSERT INTO t1 VALUES (1, 1)");

    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
