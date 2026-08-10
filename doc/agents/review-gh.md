# Posting reviews to GitHub

This document explains how to publish a patch review to a GitHub pull request.

## Required knowledge

Posting a review to GitHub means a review has to be performed first. You MUST
load `doc/agents/review.md` and follow it for the review itself. This document
only adds the publishing mechanics on top of it.

## Permissions

The only allowed actions are the following:
- Checking out the PR branch locally.
- Reading the PR details, its commit list, the diffs.
- Reading existing reviews and comments.
- Posting new review comments attached to the diffs.
- Posting PR-global comments.
- Replying to existing review comment threads.
- Reacting to existing comments of other people.
- Dropping your own not yet submitted pending review, with the comments
  collected in it (the `abort` command below).

Forbidden - anything else that writes. In particular it is not allowed to
close, reopen, approve, edit, delete PRs and comments.

By default `gh` will use the profile that the user had authenticated with. But
it can be changed using the env variable `GH_TOKEN`. It can be handy if the user
wants to post comments using a non-default profile.

Before starting to work on a review, sort out the identities, to understand
when to pass what:

- The default `gh` profile: `gh api user --jq .login`.
- The token from the local setup: the same command with `GH_TOKEN` set.
- The cookie's account: the `<login>` prefix of the `GH_COOKIE` value. The
  cookie itself cannot be checked programmatically, which is exactly why the
  login is stored inside the same value.

All the posting must run under the token of the same account the cookie
belongs to. Otherwise the private API sends the comments into an invisible
pending review of the cookie's account, while the token's own review gets
submitted empty.

## Reading commits

The diffs and the commit data is best to take from the local git repo instead of
accessing them via the web APIs. Because the web APIs often have limitations
like max number of commits listed, max number of files changed, and so on. PRs
with their base branches can be fetched like this:

```Bash
git fetch origin pull/<num>/head <base-branch>
```

A plain fetch is enough, no need to create branches and/or switch them unless
this is necessary for the review. After the fetch everything about the commits
is available locally: `git log <base-branch>..FETCH_HEAD` for the commit list
with SHAs and messages, `git show <sha>` for the diff of a single commit.

The PR metadata and the review state exist only on GitHub. `gh api` substitutes
the `{owner}` and `{repo}` placeholders from the git remote of the current
repository, so the commands below can be used literally.

- PR details:
  `gh pr view <num> --json number,title,body,state,headRefOid,url`
- Existing reviews:
  `gh api 'repos/{owner}/{repo}/pulls/<num>/reviews?per_page=100' --paginate`
- Existing review comments (the ones attached to diffs):
  `gh api 'repos/{owner}/{repo}/pulls/<num>/comments?per_page=100' --paginate`
- Existing PR-global conversation comments:
  `gh api 'repos/{owner}/{repo}/issues/<num>/comments?per_page=100' --paginate`

The list endpoints return at most one page - 30 items by default - and nothing
in the output hints that more exist, hence the `--paginate`. Mind that it
prints one JSON array per page: iterating with `--jq '.[]'` works as is, while
feeding the output to a JSON parser needs the extra `--slurp` flag.

Read the existing comments before posting anything: do not duplicate remarks
which were already made by anyone, and find the threads which expect an answer
from you. When somebody wrote a comment which you agree with, you can react to
it with a reaction or by replying there that you agree and why.

## Posting review

The review is published as one single review: each comment is attached to
the diff of a specific commit, and the review's main message carries the
PR-global comments. Plain `gh` commands cannot do this cleanly - use the
`tools/gh-review.py` wrapper (run it with `--help` for details when this
document isn't enough).

If the tool lacks something you need - a missing feature, a bug, an
inconvenience - you must suggest the user to make a patch which extends or
fixes `tools/gh-review.py`, instead of silently working around the tool
with raw API calls.

The tool works via the public GitHub API by default.

### Comment post

```Bash
tools/gh-review.py comment \
  --pr <num> \
  -c <commit sha> \
  -p <file path> \
  -l <line> \
  -m <comment text>
```

Add the comments one by one. The first invocation creates a pending review.

The lines are numbered against the diff between the PR base and the commit, not
the commit's own diff: `-l` takes the file line as of the commit, and for a
deleted line `--left` takes the line number in the PR base version of the file,
which differs from what `git show` prints.

A line which was both added and deleted between the PR base and the given commit
cannot carry a comment - anchor it at that line in the diff of the earlier
commit which added it, or at a nearby surviving line, and explain that in the
text.

A comment addresses exactly one line - for a range, anchor at its most relevant
line and describe the range in the text.

A remark about a commit message or title attaches to any line of that
commit's diff, and its text must explicitly say that it concerns the commit
message or title, not the code at that line. Same goes for commit-wide comments
about its layout, architecture, ordering.

**Experimental mode**

The `comment` command also has an experimental mode - the `--experimental`
flag - which works via the private API of the GitHub web UI and lifts some
public API limitations.

It needs a special setup which user must opt-in for in their local dev setup
(see `doc/agents/setup-dev.md`) in order for this mode to be enabled. This mode
requires a cookie which must be passed in the `GH_COOKIE` environment variable,
in the `<login>:<cookie>` format, where the login names the GitHub account the
cookie belongs to. The location of the cookie is a part of the local setup.

Unless enabled in the local setup, this mode can't be used. If the local setup
says nothing about this, then prompt the user to decide, and persist the
decision in the local setup.

In this mode the lines are numbered by the commit's own diff, exactly as
`git show` prints it: the file as of the commit, or its parent's version with
`--left`. Lines added and deleted within one patchset become commentable. A
range of lines can be commented too: `--start-line <first>` makes the comment
cover the lines from there to `-l`.

### Review status

```Bash
tools/gh-review.py status --pr <num>
```

`status` lists every collected comment with its real file line numbers,
including the ranges of the private-API comments. Mind that a `--left` anchor is
listed by its old-file line number and is not visually distinguished from a
right-side anchor at the same line.

To check the collected review before publishing.

### Review submit

```Bash
tools/gh-review.py submit --pr <num> -m <review summary text>
```

When all is ready, publish everything as one review, whose main message carries
a very short PR-global summary and comments on patchset-wide things.

### Review abort

```Bash
tools/gh-review.py abort --pr <num>
```

Drops the unsubmitted review with all its comments.

## Replying to comments

While a pending review exists, do not reply to any comments. They will be
rejected or will become a part of that pending review, which is usually
undesirable. Reply only when no pending review exists.

For threaded replies to other review comments:
```Bash
gh api repos/{owner}/{repo}/pulls/<num>/comments/<comment id>/replies \
  -f body=<reply text>
```

PR-global comments have no threads. To respond to one, post a new global
comment:
```Bash
gh pr comment <num> --body <text>
```
quoting the relevant part of the answered text with Markdown `>` quotes.

A reaction to an existing review comment (allowed values of `content` are
`+1`, `-1`, `laugh`, `confused`, `heart`, `hooray`, `rocket`, `eyes`):
```Bash
gh api repos/{owner}/{repo}/pulls/comments/<comment id>/reactions \
  -f content=+1
```

## Formatting specifics

On top of the regular review comment formatting rules, the ones below apply
specifically to GitHub.

Never use a bare `@` outside of backtick code spans: `@param`, `@TarantoolBot`
and alike trigger GitHub mentions of the users having the matching names. Wrap
such tokens in backticks. Mentioning a real user on purpose is fine.

Never write "this commit", "the previous/next commit" and alike: the GitHub web
UI does not show which commit a review comment is attached to, so such
references are unresolvable for the reader. In a PR with more than one commit,
every comment must state which commit it talks about, in a standard trailer on
the comment's last line:

    (for commit <short-hash> `<title>`)

Backtick quotes used inside the title itself are dropped, since they cannot nest
inside the backtick-quoted title.

Other commits mentioned in the comment body are named inline the same way - by
the short hash and/or the backtick-quoted title.

Never hard-wrap the prose of the comments posted to GitHub: the markdown is
reflowed by the browser, and manual line breaks render as a ragged mess. The
line length limits apply to the local files only - when the comments are
drafted in a local text/md file, wrap them there for readability, and unwrap
when posting. Code blocks keep their line structure in both places.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
