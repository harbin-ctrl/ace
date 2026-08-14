#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source_dir=${LHA_SOURCE_DIR:-$script_dir/../vendor/lha}
output=${LHA_OUTPUT:?LHA_OUTPUT is required}
jobs=${LHA_MAKE_JOBS:-2}
work=${output%/*}/lha-source

if [[ ! -f $source_dir/configure.ac || ! -f $source_dir/src/lharc.c ]]; then
    printf 'LhA source is incomplete: %s\n' "$source_dir" >&2
    exit 2
fi

# The upstream checkout deliberately does not carry generated Autotools
# files. Build in a disposable copy so autoreconf never dirties the vendored
# source tree and the normal ACE build remains reproducible from a clean tree.
rm -rf -- "$work"
mkdir -p -- "$work"
cp -a -- "$source_dir/." "$work/"

(
    cd "$work"
    autoreconf -fi
    # ACE installs the executable directly, not the Autotools package. Keep
    # the configure metadata stable instead of embedding the disposable
    # build directory in `LhA --version`.
    ./configure --prefix=/usr
    make -j"$jobs"
)

install -m 0755 "$work/src/lha" "$output"
