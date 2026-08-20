/*
 * The Regina library object set, exercised the way RexxMast uses it.
 *
 * `rexx` is the standalone interpreter and links a different object set;
 * RexxMast links the library one, calls RexxStart() to run a script and
 * IsReginaMsg() to recognise its own messages. Nothing else in ACE links
 * those objects, so without this they would compile forever and work never.
 *
 * Two things it is really checking, beyond "RexxStart returns 0":
 *
 * The result comes back. RexxStart()'s answer to a RXFUNCTION is a RXSTRING
 * the caller has to free, and it is what RexxMast puts in rm_Result2 for the
 * process that sent the message. A run that works but hands back nothing
 * would look fine here and be useless there.
 *
 * The process exits. Regina's library build carries AROS's library-lifecycle
 * hooks -- an ADD2CLOSELIB in mt_amigalib.c that walks the per-task list, and
 * ACE's stand-in for regina_init.c that owns the pool that list lives in.
 * They run as destructors, and when their order was left to the linker the
 * pool went first and CloseLib() dereferenced NULL at exit: a segfault after
 * a successful run, which a test that only checked the return value would
 * miss. Returning normally from main() is half of what this asserts.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <rexx/storage.h>

#include "rexxsaa.h"

BOOL IsReginaMsg(struct RexxMsg *message);

int main(void)
{
    /* An instore program: a length-counted source string rather than a file,
       which is the form RexxMast uses for a command it was sent. The second
       RXSTRING is Regina's slot for the tokenised form and must start empty. */
    static char program[] = "return 6 * 7";
    RXSTRING instore[2];
    RXSTRING result;
    SHORT return_code = 0;
    APIRET outcome;

    MAKERXSTRING(instore[0], program, strlen(program));
    MAKERXSTRING(instore[1], NULL, 0);
    MAKERXSTRING(result, NULL, 0);

    outcome = RexxStart(0, NULL, "ace-regina-library-test", instore,
                        "COMMAND", RXFUNCTION, NULL, &return_code, &result);
    if (outcome != 0) {
        fprintf(stderr, "regina library test: RexxStart returned %ld\n",
                (long)outcome);
        return 1;
    }
    if (!RXSTRPTR(result) || RXSTRLEN(result) != 2 ||
        memcmp(RXSTRPTR(result), "42", 2) != 0) {
        fprintf(stderr, "regina library test: result was '%.*s' (%lu bytes)\n",
                (int)RXSTRLEN(result), RXSTRPTR(result) ? RXSTRPTR(result) : "",
                (unsigned long)RXSTRLEN(result));
        return 1;
    }

    /* Not a smoke test of its own: RexxMast calls it on every message that
       arrives, so a link that resolves RexxStart but not this one is still
       a RexxMast that cannot be built. */
    assert(IsReginaMsg(NULL) == 0);

    printf("regina library test: ok\n");
    return 0;
}
