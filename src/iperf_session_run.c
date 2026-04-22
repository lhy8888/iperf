/*
 * iperf_session_run.c
 *
 * Narrow C shim that keeps the legacy setjmp/longjmp escape hatch out of the
 * C++ GUI layer.  Library/GUI code should call the wrappers in this file and
 * treat the returned code as the canonical session result.
 */

#include <setjmp.h>
#include <stdio.h>

#include "iperf.h"
#include "iperf_api.h"

static int
run_session(struct iperf_test *test, int is_server, int *escaped_by_longjmp)
{
    int rc = 0;
    int jumped = 0;

    if (escaped_by_longjmp != NULL) {
        *escaped_by_longjmp = 0;
    }
    if (test == NULL) {
        return -1;
    }

    iperf_exit_jump_ready = 1;
    if (setjmp(env) == 0) {
        rc = is_server ? iperf_run_server(test) : iperf_run_client(test);
    } else {
        rc = -1;
        jumped = 1;
    }
    iperf_exit_jump_ready = 0;

    if (escaped_by_longjmp != NULL) {
        *escaped_by_longjmp = jumped;
    }
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
