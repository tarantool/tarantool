#!/bin/bash
#
# This script checks that we can downgrade to all the released versions
# less then being released. For example when releasing version 2.11.4
# we check that we can downgrade to 2.11.3, 2.11.2, ..., 2.10.8, 2.10.7, ...
# 2.8.2.
#
# The versions we can downgrade to are those mentioned in downgrade_versions
# list in src/box/lua/upgrade.lua. For the sake of parsing simplicity
# the versions should be between begin and end marks:
# -- DOWNGRADE VERSIONS BEGIN
# -- DOWNGRADE VERSIONS END
#
# The check is only done for the commit tagged as release version
# (annotated tag with name like 2.11.4).

set -eo pipefail

error() {
   echo "$@" 1>&2
   exit 1
}

cleanup() {
  rm -f $tag_exceptions $expected_versions $actual_versions || true
}

trap "cleanup" EXIT

# Tags that should not be considered.
tag_exceptions=`mktemp`
echo 2.9.0 > $tag_exceptions

this_tag=`git describe --exact-match 2>/dev/null || true`
tag_pattern='^[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+$'
# Make downgrade check only for the release tags.
if [[ $this_tag =~ $tag_pattern ]]; then
    # File with expected versions.
    expected_versions=`mktemp`
    # Sorted (in version order) list of release tags.
    tags=`git tag | grep -E $tag_pattern | sort -V`
    skip=1
    # Cut tags below 2.8.2 and above $this_tag
    for tag in $tags; do
        if [[ $tag = '2.8.2' ]]; then
            skip=0
        fi
        if [[ $skip -eq 0 ]]; then
            echo $tag >> $expected_versions
        fi
        if [[ $tag = $this_tag ]]; then
            skip=1
        fi
    done

    # File of versions we can downgrade to.
    actual_versions=`mktemp`
    begin_mark='DOWNGRADE VERSIONS BEGIN'
    end_mark='DOWNGRADE VERSIONS END'
    upgrade_file='src/box/lua/upgrade.lua'
    grep -q "$begin_mark" $upgrade_file ||
        error "Cannot find start mark in $upgrade_file"
    grep -q "$end_mark" $upgrade_file ||
        error "Cannot find end mark in $upgrade_file"

    # Cut part of $upgrade_file between $begin_mark and $end_mark
    # and for every line strip everything except version.
    awk "/$begin_mark/{flag=1; next}
         /$end_mark/{flag=0} flag" $upgrade_file |
         sed -E 's/.*"(.*)".*/\1/' > $actual_versions

    cat $tag_exceptions >> $actual_versions
    # Sort in usual order before using `comm`.
    sort -o $expected_versions $expected_versions
    sort -o $actual_versions $actual_versions
    diff=`comm -23 $expected_versions $actual_versions`
    if [ -n "$diff" ]; then
        echo "Some versions are missing in downgrade list:" 1>&2
        echo "$diff"
        exit 1
    fi
fi

echo OK
exit 0
