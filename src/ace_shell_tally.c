#define _POSIX_C_SOURCE 200809L

#include <proto/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include "broker_protocol.h"
#include "broker_client.h"

#include <stdlib.h>

/*
 * The tally line, printed just above each prompt.
 *
 * This is ACE's cliPrompt().  The AROS one is compiled under a different name
 * -- see the -DcliPrompt= in the Makefile -- and is called at the end of this
 * one, so the prompt itself is still drawn by the shell's own code and
 * nothing about its behaviour changes.  The same technique ACE already uses
 * for Shell.c's main() and for the POSIX calls the embedded editors make: the
 * seam is a rename at compile time rather than a patch to a file that belongs
 * to somebody else.
 *
 * Here rather than in Cli(): Cli() is called several times in the course of
 * running one command -- the shell's loader asks for it too -- and a report
 * that consumed the count on each of those would print at the wrong moments
 * and be empty by the time the prompt came round.  cliPrompt() is called once
 * per prompt, which is exactly the period the count is meant to cover.
 *
 * Nothing is printed when the count is zero, so a session that needed no
 * privilege looks exactly as it did before the tally was turned on.  The
 * broker answers zero while the tally is off, so this costs one request per
 * prompt and no output.
 *
 * A shell with no prompt to draw -- a script, which is what cli_Background
 * means here -- reports nothing and, importantly, does not ask.  Asking would
 * clear the count, so a script would silently swallow the operations it
 * performed on the way past.  Leaving them to accumulate is what makes the
 * period the count covers actually be one prompt to the next: whatever an
 * Execute did in between is part of the next report, which is where the
 * person who typed it is looking.
 */
void ace_aros_cli_prompt(void *state);

void cliPrompt(void *state)
{
    struct CommandLineInterface *cli = Cli();
    char answer[64];
    char *end;
    unsigned long count;

    if (cli && !cli->cli_Background &&
        native_broker_tally(AMIGA_BROKER_TALLY_REPORT, answer,
                            sizeof(answer)) == 0) {
        count = strtoul(answer, &end, 10);
        if (end != answer && *end == '\0' && count > 0) {
            /*
             * Singular has no number in front of it.  "1 operation" reads
             * like the start of a list; the sentence is about the fact that
             * privilege was needed at all, and at one operation the count
             * adds nothing the word does not already say.
             */
            if (count == 1)
                Printf("operation required root to complete\n");
            else
                Printf("%lu operations required root to complete\n", count);
        }
    }

    ace_aros_cli_prompt(state);
}
