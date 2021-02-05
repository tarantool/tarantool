# Changelogs

## Usual process

Let's consider the example: a patchset contains several internal changes (some
refactoring) and one user visible change (bugfix, new feature, behaviour
change, new supported OS and so on). The commit that contains the user visible
change should add a file of this kind:

```markdown
$ cat changelogs/unreleased/bitset-index-varbinary.md
## bugfix/core

* Bitset index now supports the varbinary type (gh-5392).
```

The first part of the header is `bugfix` or `feature` (let's consider any use
visible change that is not a bugfix as a feature). In an *exceptional* case a
new type may be added (however now it is explicitly forbidden by the release
notes generation script).

The second part may be `core`, `memtx`, `vinyl`, `replication`, `swim`, `raft`,
`luajit`, `lua`, `sql`, `build`, `misc` and so on. Please, prefer one of those
categories, but if a change does not fit into them and more changes of this
kind are expected, feel free to add a new one.

## Add a subcategory

```markdown
## feature/lua/http client
```

## Detailed description

You may use full power of the [GitHub Flavored Markdown][gfm] syntax, but,
please, keep the source readable for humans.

Example of a detailed description:

```markdown
$ cat changelogs/unreleased/upstream-uri-password.md
## bugfix/replication

* Use empty password when a URI in `box.cfg{replication = <...>}` is like
  `login@host:port`. gh-4605. The behaviour matches the net.box's one now.
  Explicit `login:@host:port` was necessary before, otherwise a replica
  displayed the following error:

  > Missing mandatory field 'tuple' in request
```

## Leave a note to a release manager

A valuable feature or a deliberate compatibility breakage may require an extra
notice for a maintainer who will squash changelog entries into a changelog
file. Let's look on examples:

```markdown
$ cat changelogs/unreleased/stored-decimals.md
## feature/core

* Decimals can now be stored in spaces. The corresponding field type is
  introduced: `decimal`. Decimal values are also allowed in the `scalar`,
  `any`, and `number` fields. Decimal values can be indexed. gh-4333

  <..details here..>

----

Notable change: stored and indexed decimals (and new 'decimal' field type).
```

```markdown
$ cat changelogs/unreleased/session-settings.md
## feature/sql

* **[Breaking change]** Introduce _session_settings service space as
  replacement for PRAGMA keyword. gh-4511

  <..details here..>

----

Breaking change: sql: PRAGMA keyword is replaced with _session_settings system
space.
```

## Generate changelog file

This section is for a release manager. We should prepare a changelog file for a
release before the release will be tagged.

Let's do the following steps:


1. `$ ./tools/gen-release-notes > changelogs/2.7.1.md`.
2. Edit the resulting file. Comments to a release manager will be placed at
   start: usually they direct the release manager to list some changes in
   Overview or Compatibility section.
3. `$ rm changelogs/unreleased/*.md`
4. Send the changes for review (if necessary) and commit them.
5. Repeat the same for all branches to be released.

## Reasoning

We want to provide description of changes that a release offers for our users.
Not just `git log` output or a list of closed issues, but a document that is
readable and useful for developers, who use tarantool in projects. The idea is
just the same as described on the [Keep a Changelog][keep_a_changelog] website.

We started with semi-automated process that crawls closed issues and requires
*a lot* of manual work of a release manager to make the result useful for end
users.

So we decided to give more responsibility to developers and start to place the
changelog entries in cover letters. A maintainer is responsible for placing the
entry into a draft of a future release notes on GitHub's releases page. A
release manager (together with maintainers and the documentation team)
proof-reads the release notes before publishing a release.

As a side result of this process, the changelog entries becomes a subject for
review and so the experience about writing such documents is shared across the
team.

The situation becomes better, however this process have several annoying
downsides:

* A developer should remember about the changelog entry just before sending a
  patch and as result it is often get out of the mind.
* GitHub drafts are not tracked with git, accessible only through a web
  browser, poorly resolves conflicts (when several persons made changes) and
  have no protection against accidental publishing (instead of saving a draft).
* The changelog entries for upcoming releases are not available publicly prior
  to publishing the release.

So we start to look how to better store the changelogs right within the git
repository. The idea appears several times, but the stopper was that if we'll
keep the changelog within a file (just one file or one file per release,
including an upcoming release), we'll get a conflict at almost any cherry-pick:
as from a developer branch to master as well as from master to a release
branch.

The [solution][github_changelog] was suggested by the GitLab folks: just split
changelog entries across files and squash them before a release.

We follow the spirit of the [Keep a Changelog][keep_a_changelog] idea, but we
does not follow the proposed format precisely. It brings us ability to
structurize the release notes in a way that better fit our wishes and hopefully
will be most convenient for our users.

[gfm]: https://guides.github.com/features/mastering-markdown/
[keep_a_changelog]: https://keepachangelog.com/en/1.0.0/
[gitlab_changelog]: https://docs.gitlab.com/ee/development/changelog.html
