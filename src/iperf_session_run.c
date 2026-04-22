/*
 * iperf_session_run.c
 *
 * Narrow C shim that keeps the GUI/library call site on a direct return path.
 * This wrapper always returns the session result from iperf_run_client() or
 * iperf_run_server(); CLI signal handling still lives in src/main.c.
 */

#include <stdio.h>

#include "iperf.h"
#include "iperf_api.h"

static int
run_session(struct iperf_test *test, int is_server, int *escaped_by_longjmp)
{
    int rc = 0;

    if (escaped_by_longjmp != NULL) {
        *escaped_by_longjmp = 0;
    }
    if (test == NULL) {
        return -1;
    }

    rc = is_server ? iperf_run_server(test) : iperf_run_client(test);
    return rc;
}

int
iperf_run_client_session(struct iperf_test *test, int *escaped_by_longjmp)
{
    return run_session(test, 0, escaped_by_longjmp);
}

int
iperf_run_server_session(struct iperf_test *test, int *escaped_by_longjmp)
{
    return run_session(test, 1, escaped_by_longjmp);
}
