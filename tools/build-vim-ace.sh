#!/bin/bash

set -eu

if [[ -z "${VIM_SRC:-}" || ! -d "$VIM_SRC/src" ]]; then
    echo "build-vim-ace.sh: set VIM_SRC to an untouched Vim checkout" >&2
    exit 2
fi

ace_root=${ACE_ROOT:?}
ace_build=${ACE_BUILD:?}
vim_src=$VIM_SRC/src
object_dir=$ace_build/vim-objects
mkdir -p "$object_dir"
if [[ ! -d "$VIM_SRC/runtime" ]]; then
    echo "build-vim-ace.sh: Vim checkout lacks runtime/" >&2
    exit 2
fi
mkdir -p "$ace_build/runtime"
cp -a "$VIM_SRC/runtime/." "$ace_build/runtime/"

read -r -a compiler <<< "${CC:-cc}"
common_flags=(
    -std=c11 -Wall -Wextra
    -Wno-return-mismatch
    -Wno-unused-parameter -Wno-pointer-sign -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-implicit-function-declaration
    -Wno-int-conversion -Wno-int-to-pointer-cast -Wno-sign-compare
    -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
    -DAMIGA -D__AROS__ -DFEAT_NORMAL
    -DHAVE_STRCASECMP -DHAVE_STRNCASECMP
    -include strings.h -include sys/types.h -include sys/time.h
    -I"$ace_root/compat/vim/include"
    -I"$vim_src/proto" -I"$vim_src"
    -I"$ace_root/compat/include" -I"$ace_root/src"
)
vim_path_flags=(
    -Dopen=ace_vim_open -Dfopen=ace_vim_fopen
    -Dstat=ace_vim_stat -Dlstat=ace_vim_lstat -Daccess=ace_vim_access
    -Dremove=ace_vim_remove -Drename=ace_vim_rename
)
vim_probe_flags=(-Dmch_isdir=ace_vim_isdir -Dmch_getperm=ace_vim_getperm)

compile_vim() {
    local source=$1
    local name=${source##*/}
    local path_flags=("${vim_path_flags[@]}" "${vim_probe_flags[@]}")
    name=${name%.c}
    if [[ "$source" == os_amiga.c ]]; then
        path_flags=("${vim_path_flags[@]}")
    fi
    "${compiler[@]}" "${common_flags[@]}" "${path_flags[@]}" \
        "$vim_src/$source" \
        -c -o "$object_dir/$name.o"
}

if [[ ! -f "$vim_src/proto/os_amiga.pro" ]]; then
    echo "build-vim-ace.sh: Vim checkout lacks proto/os_amiga.pro" >&2
    echo "refusing to generate files in the Vim source tree" >&2
    exit 2
fi

# The source list is Vim's own normal-feature list. os_unix and pathdef are
# replaced by the Amiga backend and ACE's path metadata; gui_xim is not part of
# an Amiga terminal build.
awk '/^BASIC_SRC =/{inside=1; next} inside && /^SRC =/{exit} inside {
    gsub(/\\/, "")
    for (i = 1; i <= NF; i++)
        if ($i ~ /^[A-Za-z0-9_]+\.c$/) print $i
}' "$vim_src/Makefile" | while read -r source; do
    case "$source" in
        os_unix.c|auto/pathdef.c|gui_xim.c) ;;
        *) compile_vim "$source" ;;
    esac
done
compile_vim os_amiga.c
compile_vim os_amiga_stubs.c
for source in xdiff/xdiffi.c xdiff/xemit.c xdiff/xprepare.c \
              xdiff/xutils.c xdiff/xhistogram.c xdiff/xpatience.c; do
    compile_vim "$source"
done

"${compiler[@]}" "${common_flags[@]}" -I"$vim_src" -I"$vim_src/proto" \
    "$ace_root/src/ace_vim_compat.c" -c -o "$object_dir/ace_vim_compat.o"
"${compiler[@]}" "${common_flags[@]}" -I"$vim_src" -I"$vim_src/proto" \
    "$ace_root/src/ace_vim_files.c" -c -o "$object_dir/ace_vim_files.o"
"${compiler[@]}" "${common_flags[@]}" -I"$vim_src" -I"$vim_src/proto" \
    "$ace_root/src/ace_vim_pathdef.c" -c -o "$object_dir/ace_vim_pathdef.o"
"${compiler[@]}" -std=c11 -Wall -Wextra -Werror -I"$ace_root/compat/include" \
    "$ace_root/src/ace_vim_editor_stubs.c" -c \
    -o "$object_dir/ace_vim_editor_stubs.o"
"${compiler[@]}" -std=c11 -Wall -Wextra -Werror -O2 -ffunction-sections \
    -fdata-sections -I"$ace_root/compat/include" -I"$ace_root/src" \
    -c "$ace_root/src/native_dos.c" -o "$object_dir/native_dos.o"

vim_objects=()
while IFS= read -r object; do
    vim_objects+=("$object")
done < <(find "$object_dir" -maxdepth 1 -name '*.o' \
    ! -name 'native_dos.o' ! -name 'ace_vim_compat.o' \
    ! -name 'ace_vim_files.o' \
    ! -name 'ace_vim_pathdef.o' ! -name 'ace_vim_editor_stubs.o' \
    -print)

ace_objects=(
    "$object_dir/native_dos.o"
    "$object_dir/ace_vim_compat.o"
    "$object_dir/ace_vim_files.o"
    "$object_dir/ace_vim_pathdef.o"
    "$object_dir/ace_vim_editor_stubs.o"
    "$ace_build/ace-vim-runtime.o"
    "$ace_build/broker_client.o"
    # broker_client.c asks broker_identity.c where this system's socket is.
    "$ace_build/broker-identity.o"
    "$ace_build/native_process.o"
    "$ace_build/native_command.o"
    # native_command.c raises the Amiga break signals through the exec
    # runtime, which is where a task's signal state lives; the runtime in
    # turn answers for the clipboard device, and Delete() tells the bridge
    # what it just removed.
    "$ace_build/aros-exec-runtime.o"
    "$ace_build/clipboard-device.o"
    "$ace_build/clipboard-bridge.o"
    "$ace_build/assign_compat.o"
    # A raw Read() on stdin reads the descriptor through the console
    # channel, which is the path native_dos.c keeps for unchanged Amiga
    # programs like Vim. The endpoint on the other side of that same Read()
    # belongs to the shell's console handler and is stubbed instead: see
    # src/ace_vim_editor_stubs.c for why it cannot be linked here.
    "$ace_build/console_channel.o"
    "$ace_build/aros-dos-getdeviceproc.o"
    "$ace_build/aros-dos-freedeviceproc.o"
    "$ace_build/aros-dos-matchfirst.o"
    "$ace_build/aros-dos-matchnext.o"
    "$ace_build/aros-dos-matchend.o"
    "$ace_build/aros-dos-match_misc.o"
    "$ace_build/aros-dos-matchpattern.o"
    "$ace_build/aros-dos-parsepattern.o"
    "$ace_build/aros-dos-matchpatternnocase.o"
    "$ace_build/aros-dos-parsepatternnocase.o"
    "$ace_build/aros-dos-patternmatching.o"
)

"${compiler[@]}" -Wl,--gc-sections "${vim_objects[@]}" "${ace_objects[@]}" \
    -lm -pthread -o "$ace_build/vim"
