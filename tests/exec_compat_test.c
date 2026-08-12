#include "exec_compat.h"

#include <exec/memory.h>
#include <proto/exec.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

struct worker_context {
    struct amiga_exec_task *main_task;
    int signal_number;
};

static void *signal_worker(void *context)
{
    struct worker_context *worker = context;
    struct amiga_exec_task *task = amiga_exec_current_task();
    int signal_number = amiga_exec_alloc_signal(task, 4);

    assert(signal_number == 4);
    assert(amiga_exec_signal(worker->main_task, 1u << worker->signal_number) ==
           0);
    assert(amiga_exec_wait(amiga_exec_current_task(), 1u << 4) ==
           (1u << 4));
    assert(amiga_exec_free_signal(task, signal_number) == 0);
    return (void *)(uintptr_t)42;
}

struct device_context {
    int opens;
    int closes;
};

static int test_device_open(void *context, unsigned long unit,
                            unsigned long flags, void **handle_out)
{
    struct device_context *device = context;

    assert(unit == 3);
    assert(flags == 7);
    device->opens++;
    *handle_out = device;
    return 0;
}

static void test_device_close(void *context, void *handle)
{
    struct device_context *device = context;

    assert(handle == device);
    device->closes++;
}

int main(void)
{
    struct amiga_exec_task *main_task;
    struct amiga_exec_task *worker_task;
    struct worker_context worker;
    struct amiga_exec_msg_port *port;
    struct amiga_exec_message message = {0};
    struct amiga_exec_message *received;
    struct device_context device = {0};
    int signal_number;
    void *memory;
    void *handle;
    void *result;
    int library_base;
    APTR generic_memory;
    LONG generic_signal;
    struct amiga_exec_msg_port *generic_port;

    assert(amiga_exec_register_current_task("test-main", &main_task) == 0);
    assert(FindTask(NULL) == main_task);
    generic_memory = AllocMem(16, MEMF_CLEAR);
    assert(generic_memory != NULL);
    for (size_t index = 0; index < 16; index++)
        assert(((unsigned char *)generic_memory)[index] == 0);
    FreeMem(generic_memory, 16);
    generic_signal = AllocSignal(-1);
    assert(generic_signal >= 0);
    assert(SetSignal(1u << generic_signal, 0) == 0);
    assert(Wait(1u << generic_signal) == (1u << generic_signal));
    FreeSignal(generic_signal);
    assert(strcmp(amiga_exec_task_name(main_task), "test-main") == 0);
    memory = amiga_exec_alloc_mem(32, AMIGA_EXEC_MEMF_CLEAR);
    assert(memory != NULL);
    for (size_t index = 0; index < 32; index++)
        assert(((unsigned char *)memory)[index] == 0);
    amiga_exec_free_mem(memory, 32);
    signal_number = amiga_exec_alloc_signal(main_task, -1);
    assert(signal_number >= 0);
    assert(amiga_exec_set_signal(main_task, 1u << signal_number, 0) == 0);
    assert(amiga_exec_check_signal(main_task, 1u << signal_number) ==
           (1u << signal_number));
    assert(amiga_exec_set_signal(main_task, 0, 1u << signal_number) ==
           (1u << signal_number));
    assert(amiga_exec_check_signal(main_task, 1u << signal_number) == 0);

    worker.main_task = main_task;
    worker.signal_number = signal_number;
    assert(amiga_exec_create_task("test-worker", signal_worker, &worker,
                                  &worker_task) == 0);
    assert(amiga_exec_wait(main_task, 1u << signal_number) ==
           (1u << signal_number));
    assert(amiga_exec_signal(worker_task, 1u << 4) == 0);
    assert(amiga_exec_join_task(worker_task, &result) == 0);
    assert((uintptr_t)result == 42);
    amiga_exec_delete_task(worker_task);

    assert(amiga_exec_create_msg_port(main_task, signal_number, "test-port",
                                      &port) == 0);
    message.payload = (void *)"payload";
    assert(amiga_exec_put_msg(port, &message) == 0);
    assert(amiga_exec_wait(main_task, 1u << signal_number) ==
           (1u << signal_number));
    received = amiga_exec_get_msg(port);
    assert(received == &message);
    assert(strcmp(received->payload, "payload") == 0);
    amiga_exec_delete_msg_port(port);

    assert(amiga_exec_register_library("test.library", 36, &library_base) ==
           0);
    assert(amiga_exec_open_library("TEST.LIBRARY", 35) == &library_base);
    assert(OpenLibrary("test.library", 35) == (struct Library *)&library_base);
    assert(amiga_exec_open_library("test.library", 37) == NULL);
    CloseLibrary((struct Library *)&library_base);
    assert(amiga_exec_close_library("test.library", &library_base) == 0);
    assert(amiga_exec_unregister_library("test.library", &library_base) == 0);

    generic_port = CreateMsgPort();
    assert(generic_port != NULL);
    DeleteMsgPort(generic_port);

    assert(amiga_exec_register_device("test.device", &device,
                                     test_device_open, test_device_close) == 0);
    assert(amiga_exec_open_device("TEST.DEVICE", 3, 7, &handle) == 0);
    assert(handle == &device);
    assert(amiga_exec_close_device("test.device", handle) == 0);
    assert(device.opens == 1 && device.closes == 1);
    assert(amiga_exec_unregister_device("test.device") == 0);

    assert(amiga_exec_free_signal(main_task, signal_number) == 0);
    amiga_exec_unregister_current_task(main_task);
    return 0;
}
