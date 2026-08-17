#include <assert.h>
#include <string.h>

#include "console_spec.h"

int main(void)
{
    struct ace_console_spec spec;

    assert(ace_console_spec_parse("CON:", &spec) == 0);
    assert(!spec.has_position);
    assert(!spec.has_size);
    assert(spec.title[0] == '\0');

    assert(ace_console_spec_parse(
               "CON:10/50/640/480/AROS-Shell/CLOSE", &spec) == 0);
    assert(spec.has_position);
    assert(spec.has_size);
    assert(spec.x == 10);
    assert(spec.y == 50);
    assert(spec.width == 640);
    assert(spec.height == 480);
    assert(strcmp(spec.title, "AROS-Shell") == 0);

    assert(ace_console_spec_parse("CON:////Named", &spec) == 0);
    assert(!spec.has_position);
    assert(!spec.has_size);
    assert(strcmp(spec.title, "Named") == 0);
    assert(ace_console_spec_parse("FILE:foo", &spec) != 0);
    assert(ace_console_spec_parse("CON:1/2/-3/4/Bad", &spec) != 0);
    return 0;
}
