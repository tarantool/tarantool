#!/bin/bash
# vim: set ts=4 sts=4 sw=4 et:
#
# This script checks that current branch has most recent entrypoint tag.
# Entrypoint tag is annotated tag. Release tags are also expected to
# be annotated tags.
#
# There are 2 cases when we require entrypoint tag.
#
# 1. We require that after release tag (like 2.11.3) the next commit
# has entrypoint tag (2.11.4-entrypoint).
#
# 2. After branching. For example we develop 3.0.0 in master. And decide
# to create 3.0 branch. The first commint in 3.0 branch is required
# to have some tag (like 3.0.0-rc1, 3.0.0 or whatever). The first commit after
# fork in master branch is required to have  entrypoint tag (3.1.0-entrypoint).
#
# Note that in both cases we do not check that entrypoint tag has proper
# suffix or numbers.
#
# We check for most recent entrypoint tag only. For example if there
# are tags 2.10.0 and 2.10.1 we only check for 2.10.2-entrypoint.
#
# Expected branches names:
#
# - master
# - release/*

set -eo pipefail

error() {
   echo "$@" 1>&2
   exit 1
}

if ! git --version >/dev/null 2>&1; then
    error "git must be installed."
fi

#########
# Case 1.
#########

# Match digit only release tags like 2.10.0.
pattern='^[[:digit:]]+\.[[:digit:]]+\.[[:digit:]]+$'
# Get most recent tag in the HEAD ancestry.
tag=`git describe --abbrev=0`
# If it is a release tag.
if [[ "$tag" =~ $pattern ]]; then
    # Find the commit just after the release tag in the HEAD ancestry.
    # It is not tagged as entrypoint because it was not seen by the
    # describe command above.
    entrypoint=`git rev-list HEAD ^$tag | tail -n1`
    if [[ $entrypoint ]]; then
        error "Missing entrypoint tag for commit $entrypoint after release"\
              "tag $tag."
    fi
fi

#########
# Case 2.
#########

# Find current branch (report HEAD for 'detached HEAD' state).
branch=`git rev-parse --abbrev-ref HEAD`
if [[ "$branch" =~ ^(master|release/.*)$ ]]; then
    # We need to find the commit that starts this branch (i.e. that the first
    # commit on this branch after the commit that is common for two branches.)
    #
    # In order to archive this we find all the commits of this branch that
    # are not on other branches from release/* && master set.
    #
    # Unfortunately I did not find a way to set arguments for git rev-list
    # without this branch.
    if [[ "$branch" = master ]]; then
        not_remotes="--remotes=origin/release/*"
    else
        not_remotes="--exclude origin/$branch --remotes=origin/release/* origin/master"
    fi
    entrypoint=`git rev-list HEAD --not $not_remotes | tail -n1`
    if [[ $entrypoint ]]; then
        # Check if entrypoint has annotated tag.
        git describe --exact-match $entrypoint &>/dev/null || \
        error "Missing tag for commit $entrypoint after branching in"\
              "branch $branch."
    fi
fi

echo OK
