#!/bin/sh

DAY=`date +%d.%m.%Y`

git config --global --add safe.directory /opt/code

TAG=`git describe --tags --abbrev=0`

env

aclocal
autoheader
automake -a
autoconf

./configure --host=i386-aros --disable-iconv --disable-largefile --with-tmp-file=no --with-additional-suffixes="lha," CFLAGS="-DEXPAND_WILDCARDS -D__VER__=$TAG -D__DAY__=$DAY"

make -C dist
