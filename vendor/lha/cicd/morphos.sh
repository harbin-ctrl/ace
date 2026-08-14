#!/bin/sh

DAY=`date +%d.%m.%Y`

git config --global --add safe.directory /gg/code

TAG=`git describe --tags --abbrev=0`

env

aclocal
autoheader
automake -a
autoconf

./configure --host=ppc-morphos --disable-iconv --with-tmp-file=no --with-additional-suffixes="lha," CFLAGS="-D__IXEMUL_EXEC_MIXING_ALLOWED -DEXPAND_WILDCARDS -D__VER__=$TAG -D__DAY__=$DAY"

make -C dist
