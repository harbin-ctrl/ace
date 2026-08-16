ED 2.00 probe for AmigaOS 3.1
================================

This probe is intended to be copied to Work:, which is mapped by the local
Amiberry-Lite configuration to /home/pi/Amiberry-Lite/work.

In the Amiga Shell, run:

    EXECUTE Work:ed-probe/run

The script recreates only Work:ed-probe/results, then runs ED several times
using WITH command files.  The results drawer contains both the files ED
saved and the raw console output from each session.  The .raw files are
important: they preserve ED's cursor movement, screen clearing, prompts, and
status messages for comparison with Tine.

The probe is deliberately non-interactive.  It does not modify anything
outside Work:ed-probe/results and the files it creates there.  Re-running it
deletes only the contents of that results drawer.

After it finishes, leave the emulator running.  The host-side results are in:

    /home/pi/Amiberry-Lite/work/ed-probe/results

If a particular ED session stops instead of returning to the Shell, press
Ctrl-C and leave the files already produced in results; they are still useful.
