#!/bin/bash
if [ "$1" == "--cc" ]; then
        cxx=$2
        shift 2
fi
if [ "$1" == "--incs" ]; then
        incs=$2
        shift 2
fi
if [ "$1" == "--extra" ]; then
        extra_inc=$2
        shift 2
fi
if [ "$1" == "--src" ]; then
        srcfile=$2
        shift 2
fi

Incs=$(echo $incs | sed -e 's/\([^ ]*\)\ */ -I\1/g')
# 1. Preprocess using C++ mode and keeping comments
# 2. cleanup irrelevant preprocessor instructions first
# 3. then leave only between \cond ffi .. \endcond ffi
# 4. and once again preprocess to remove comments

$cxx -E -CC $Incs -I $extra_inc $srcfile | \
sed -e '/^#/d' -e '/^$/d' | \
sed -n '/^\/\*\* \\cond ffi \*\/$/,/^\/\*\* \\endcond ffi \*\/$/P' | \
$cxx -E -P -
