ED 2.00 probe for AmigaOS 3.1
================================

This probe is intended to be copied to Work:.  The
tools/amiga-ed-fresh-run harness maps Work: to a fresh host directory for
each run.

The complete fresh FS-UAE chassis can be run from Linux with:

    tools/amiga-ed-fresh-run

It defaults to the Amiga Forever AmigaOS 3.1 A1200 ROM.  Override the
emulated model or ROM with AMIGA_MODEL and AMIGA_ROM when needed.

For a manually prepared Work: volume, run from the Amiga Shell:

    EXECUTE Work:ed-probe/run

The script recreates only Work:ed-probe/results, then runs ED several times
using WITH command files.  The results drawer contains both the files ED
saved and per-session output files.  With WINDOW CON: the editor's screen
output remains on the emulated console, so use a visible run when examining
cursor movement, prompts, and status messages; DOS redirection alone does not
capture ED's screen output.

The command files intentionally put one extended command on each line, as
required by ED 2.00's WITH-file reader.  Automated sessions end with X:
within a WITH file, Q stops reading that file and returns to editing, while
the interactive ESC-Q sequence is the separate unsaved-quit operation.
The probe uses ED's I (insert before) and A (insert after) commands; TY is an
EDIT command and is intentionally not used here.  ED's U command can undo
changes on the current line, but cannot undo a deleted line.

The SH/status-page check is in `status-run` rather than the batch probe.  SH
pauses for an actual keystroke, so after running it, dismiss the page, press
ESC, enter X, and press Return to leave ED.

The probe is deliberately non-interactive.  It does not modify anything
outside Work:ed-probe/results and the files it creates there.  Re-running it
deletes only the contents of that results drawer.

After it finishes, the harness stops the emulator and prints the fresh host
run root and results directory.

If a particular ED session stops instead of returning to the Shell, press
Ctrl-C and leave the files already produced in results; they are still useful.
