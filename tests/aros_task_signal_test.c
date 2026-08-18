#include "aros_exec_runtime.h"

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <dos/dosextens.h>

static void *send_host_break(void *unused)
{
    struct timespec delay = {0, 20000000L};

    (void)unused;
    nanosleep(&delay, NULL);
    assert(kill(getpid(), SIGUSR1) == 0);
    return NULL;
}

int main(void)
{
    struct Task first = {0};
    struct Task second = {0};

    first.tc_Node.ln_Name = "first-task";
    second.tc_Node.ln_Name = "second-task";
    assert(ace_aros_runtime_register_task(&first) == 0);
    assert(ace_aros_runtime_register_task(&second) == 0);
    assert(ace_aros_runtime_find_task("first-task") == &first);
    assert(ace_aros_runtime_find_task("second-task") == &second);

    ace_aros_runtime_set_current_task(&first);
    assert(ace_aros_runtime_set_signal(1u << 4, 0) == 0);
    assert(ace_aros_runtime_alloc_signal(7) == 7);
    assert(ace_aros_runtime_alloc_signal(7) == -1);

    ace_aros_runtime_set_current_task(&second);
    assert(ace_aros_runtime_check_signal(~0u) == 0);
    assert(ace_aros_runtime_alloc_signal(7) == 7);
    ace_aros_runtime_signal_task(&first, 1u << 5);
    assert(ace_aros_runtime_check_signal(~0u) == 0);

    ace_aros_runtime_set_current_task(&first);
    assert(ace_aros_runtime_check_signal(~0u) == ((1u << 4) | (1u << 5)));
    assert(Wait(1u << 4) == (1u << 4));
    assert(ace_aros_runtime_check_signal(~0u) == (1u << 5));
    ace_aros_runtime_set_signal(0, 1u << 5);

    /* The host signal handler uses a pipe handoff; Wait() is awakened by the
       dispatcher, rather than polling for an arbitrary interval. */
    {
        pthread_t sender;

        assert(pthread_create(&sender, NULL, send_host_break, NULL) == 0);
        assert(Wait(1u << 12) == (1u << 12));
        assert(pthread_join(sender, NULL) == 0);
    }
    ace_aros_runtime_free_signal(7);
    ace_aros_runtime_unregister_task(&second);
    assert(ace_aros_runtime_find_task("second-task") == NULL);
    ace_aros_runtime_signal_task(&second, 1u << 6);
    assert(ace_aros_runtime_check_signal(~0u) == 0);
    ace_aros_runtime_unregister_task(&first);
    return 0;
}
