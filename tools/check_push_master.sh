#!/bin/bash

# This script is recommended to run before pushing master to origin.
# It fails (returns 1) if a problem detected, see output.
exit_status=0

# Check that submodules' commits are from their master branches.
# The script checks all submodules, but fails only for the following list:
our_submodules="src/lib/msgpuck:src/lib/small:test-run"

function check_submodule() {
    local submodule_path=$1
    local commit=$2
    git "--git-dir=$(git rev-parse --show-toplevel)/$submodule_path/.git" branch -r --contains $commit | egrep " origin/master$" > /dev/null
    local commit_is_from_master=$?
    [[ ":$our_submodules:" =~ ":$submodule_path:" ]]
    local it_is_our_submodule=$?
    if [[ $it_is_our_submodule -eq 0 ]] && [[ $commit_is_from_master -eq 1 ]]; then
        echo "Submodule $submodule_path is set to commit $commit that is not in master branch!"
        exit_status=1
    fi
}

while read -r line; do
    check_submodule $line
done <<<"`git "--git-dir=$(git rev-parse --show-toplevel)/.git" ls-tree -r HEAD | egrep "^[0-9]+ commit" | awk '{print $4 " " $3}'`"

# Done
exit $exit_status