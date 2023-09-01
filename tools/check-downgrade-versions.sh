#!/bin/bash
# vim: set ts=4 sts=4 sw=4 et:

set -eo pipefail

upgrade_lua=src/box/lua/upgrade.lua
# Get checkouted tag.
current_tag=`git describe --exact-match HEAD`

# List of versions supported by downgrade.
# awk cuts versions from sources using markers. sed filters versions only
# (symbols between "").
versions=`mktemp`
awk '/DOWNGRADE VERSIONS START/{flag=1; next}
     /DOWNGRADE VERSIONS END/{flag=0} flag' $upgrade_lua |
     sed -E 's/.*"(.*)".*/\1/' > $versions

last=`tail -n1 $versions`
first=`head -n1 $versions`

if [[ $last != $current_tag ]]; then
    echo "Last downgrade version should be the tag being released."
    exit 1
fi

# Sorted list of tags from git starting from $first and ending at $last
# (inclusively).
# Last awk do the cut [$first, $last].
tags=`mktemp`
git tag -l -n1 | grep stable | sort -V | awk '{print $1}' |
    awk "/$first/{flag=1} flag; /$last/{flag=0}" > $tags


# Use || or 'set -e' mode will exit here.
result=0
diff=`mktemp`
diff -u $versions $tags >$diff || result=1

if [[ $result -ne 0 ]]; then
    echo 'Downgrade version list is not correct:'
    echo '  + marks missing versions'
    echo '  - marks non-existing versions'
    sed '/^\(---\|+++\) /d' $diff
fi

rm -rf $versions $tags $diff
exit $result
