#include "console_device.h"
#include "con_handler.h"

#include <assert.h>
#include <string.h>

struct test_context {
    char output[32];
    size_t output_length;
};

static int test_read(void *context, void *data, size_t length, size_t *actual)
{
    const char input[] = "input";
    size_t count = sizeof(input) - 1;

    (void)context;
    if (count > length)
        count = length;
    memcpy(data, input, count);
    *actual = count;
    return AMIGA_IOERR_OK;
}

static int test_write(void *context, const void *data, size_t length,
                      size_t *actual)
{
    struct test_context *test = context;

    assert(length <= sizeof(test->output) - test->output_length);
    memcpy(test->output + test->output_length, data, length);
    test->output_length += length;
    *actual = length;
    return AMIGA_IOERR_OK;
}

int main(void)
{
    struct test_context context = {0};
    struct amiga_console_device device = {
        .context = &context,
        .read = test_read,
        .write = test_write,
    };
    struct amiga_console_unit *unit = NULL;
    struct amiga_console_unit *queued_unit = NULL;
    struct amiga_console_device queued_device = {
        .context = &context,
        .write = test_write,
    };
    struct amiga_con_file file = {0};
    struct amiga_con_file queued_file = {0};
    struct amiga_con_handler handler;
    char input[8] = {0};
    struct amiga_console_io_request request = {
        .command = AMIGA_CMD_WRITE,
        .data = (void *)"async",
        .length = 5,
    };
    struct amiga_console_io_request abort_request = {
        .command = AMIGA_CMD_READ,
        .data = input,
        .length = sizeof(input),
    };
    size_t actual;

    assert(amiga_console_OpenDevice(&device, &unit) == AMIGA_IOERR_OK);
    assert(amiga_con_Open(unit, &file) == AMIGA_IOERR_OK);
    assert(amiga_con_Write(&file, "output", 6, &actual) == AMIGA_IOERR_OK);
    assert(actual == 6);
    assert(context.output_length == 6);
    assert(memcmp(context.output, "output", 6) == 0);
    assert(amiga_con_Read(&file, input, sizeof(input), &actual) ==
           AMIGA_IOERR_OK);
    assert(actual == 5);
    assert(memcmp(input, "input", 5) == 0);
    assert(amiga_console_SendIO(unit, &request) == AMIGA_IOERR_OK);
    assert(amiga_console_WaitIO(&request) == AMIGA_IOERR_OK);
    assert(request.actual == 5);
    assert(amiga_console_OpenDevice(&queued_device, &queued_unit) ==
           AMIGA_IOERR_OK);
    assert(amiga_con_Open(queued_unit, &queued_file) == AMIGA_IOERR_OK);
    assert(amiga_con_handler_Open(queued_unit, &handler) == AMIGA_IOERR_OK);
    assert(amiga_con_handler_FeedInput(&handler, "one", 3) ==
           AMIGA_IOERR_OK);
    assert(amiga_con_handler_FeedInput(&handler, "\n", 1) ==
           AMIGA_IOERR_OK);
    memset(input, 0, sizeof(input));
    assert(amiga_con_handler_Read(&handler, input, sizeof(input), &actual) ==
           AMIGA_IOERR_OK);
    assert(actual == 4);
    assert(memcmp(input, "one\n", 4) == 0);
    assert(amiga_con_handler_SetRaw(&handler, 1) == AMIGA_IOERR_OK);
    assert(amiga_con_handler_FeedInput(&handler, "\033[A", 3) ==
           AMIGA_IOERR_OK);
    memset(input, 0, sizeof(input));
    assert(amiga_con_handler_Read(&handler, input, 3, &actual) ==
           AMIGA_IOERR_OK);
    assert(actual == 3);
    assert(memcmp(input, "\033[A", 3) == 0);
    assert(amiga_con_handler_SetRaw(&handler, 0) == AMIGA_IOERR_OK);
    assert(amiga_console_SendIO(queued_unit, &abort_request) ==
           AMIGA_IOERR_OK);
    assert(amiga_console_AbortIO(queued_unit, &abort_request) ==
           AMIGA_IOERR_OK);
    assert(amiga_console_WaitIO(&abort_request) == AMIGA_IOERR_ABORTED);
    assert(amiga_console_FeedInput(queued_unit, "queued", 6) ==
           AMIGA_IOERR_OK);
    memset(input, 0, sizeof(input));
    assert(amiga_con_Read(&queued_file, input, sizeof(input), &actual) ==
           AMIGA_IOERR_OK);
    assert(actual == 6);
    assert(memcmp(input, "queued", 6) == 0);
    amiga_con_handler_Close(&handler);
    amiga_con_Close(&queued_file);
    amiga_console_CloseDevice(queued_unit);
    amiga_con_Close(&file);
    amiga_console_CloseDevice(unit);
    return 0;
}
