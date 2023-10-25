#!/bin/bash
# vim: set ts=4 sts=4 sw=4 et:
#
# This script checks that current branch has most recent entrypoint tag.
# There are 2 cases when we require entrypoint tag.
#
# 1. After release tag (like 2.10.5, 2.11.0 etc). We require that the next
# commit has annotated tag
#
# 2. After branching. For example we develop 3.0.0 in master. And decide
# to release 3.0.0. We fork release/3.0 branch. The first commit after
# fork in release/3.0 branch is required to has annotated tag (like 3.0.0-rc1
# or something). The first commit after fork in master branch is required
# to has annotated entrypoint tag too.
#
# Note that in both cases we do not check that required annotated tag has
# "entrypoint" or "rc" in the name.
#
# We check for most recent entrypoint tag only. Most recent in a sense that
# for example if there are tags for releases 2.10.0 and 2.10.1 on current
# branch the script only check for entrypoint tag after 2.10.1 tag.

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
# Show most recent tag in the HEAD ancestry
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
if [[ "$branch" =~ ^(nshy/master|nshy/release/.*)$ ]]; then
    # We need to find the commit that starts this branch (i.e. that the first
    # commit on this branch after the commit that is common for two branches.)
    #
    # In order to archive this we find all the commits of this branch that
    # are not on other branches from release/* && master set.
    #
    # Unfortunately I did not find a way to set arguments for git rev-list
    # without this branch.
    if [[ "$branch" = nshy/master ]]; then
        not_remotes="--remotes=origin/nshy/release/*"
    else
        not_remotes="--exclude origin/$branch --remotes=origin/nshy/release/* origin/nshy/master"
    fi
    entrypoint=`git rev-list HEAD --not $not_remotes | tail -n1`
    if [[ $entrypoint ]]; then
        # Show most recent tag in the HEAD ancestry
        tag=`git describe --abbrev=0`
        tag_commit=`git rev-list -n 1 $tag`
        if [[ $tag_commit != $entrypoint ]]; then
            error "Missing tag for commit $entrypoint after branching."\
                  "It should be an entrypoint tag or a tag for a downstream"\
                  "branch."
        fi
    fi
fi

echo OK
