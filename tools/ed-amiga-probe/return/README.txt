Focused ED 2.00 command-return corpus
=====================================

This corpus tests one behavior only: Return submits an interactive extended
command. In the ED screen, type `alpha`, press ESC, type `X`, and press
Return. ED must save the edited file and return to the Shell.

Run it through the fresh real-Amiga chassis with:

    FS_UAE_WINDOW_HIDDEN=0 ED_PROBE_DIR=tools/ed-amiga-probe/return \
        tools/amiga-ed-fresh-run

The resulting `return.txt` containing `alpha` is the real ED 2.00 golden
output. The run ends when ED returns to the Shell and writes `COMPLETE`.
