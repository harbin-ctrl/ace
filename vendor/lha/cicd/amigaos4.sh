#!/bin/sh

DAY=`date +%d.%m.%Y`

git config --global --add safe.directory /opt/code

TAG=`git describe --tags --abbrev=0`

env

aclocal
autoheader
automake -a
autoconf

./configure --host=ppc-amigaos --disable-iconv --with-tmp-file=no --with-additional-suffixes="lha," CFLAGS="-D_XOPEN_SOURCE -DUSE_OLD_ANCHORPATH -D__USE_INLINE__ -DEXPAND_WILDCARDS -D__VER__=$TAG -D__DAY__=$DAY"

make -C dist
