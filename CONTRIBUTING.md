# Contributing to Tarantool

You likely come here from the issues or pull requests page. There are some
suggestions on how to make your contribution most useful.

## File an issue

Consider using a bug report/feature request template. Feel free to adapt it for
your needs. You can opt out and start from a blank issue, but be mindful of the
completeness of the information.

There are feature requests of different kinds:

* A complement to existing functionality or another ready-to-implement request.
* A new idea or something else that requires a discussion.

The former is completely okay to be asked via an issue. For the latter kind of
a request consider opening a discussion in the [Ideas][ideas] category. It
helps to keep the issue tracker actual and clean rather than a bag of thoughts.

Questions could be asked in discussions in the [Q&A][q-a] category.

## Open a pull request

There are many aspects which a programmer should take care of. Here is a short
list of simple rules, which aims to reduce ping-pong between a developer and a
reviewer.

1. Target a pull request to the `master` branch.
2. Don't forget to:
   - Add a [changelog entry][changelogs]. Follow suggestions from the
     [How to write release notes][release-notes] document.
   - Add a [documentation request][docbot] into the commit message. Example:

     ```
     @TarantoolBot document
     Title: One-line title
     Description that can be multiline,
     contain markup and links.
     ```
   - Add a test (as for a feature, as well as for a bugfix). Prefer `luatest`
     over other test types.

   Add `NO_CHANGELOG=<reason>`, `NO_DOC=<reason>`, and/or `NO_TEST=<reason>`
   into a commit message if some of those bullets are not applicable. Every
   commit in a patchset is validated against those rules.
3. Adhere [How to write a commit message][commit-message] rules. Highlights:
   - Describe a reason of the change, **why** it is necessary (see rule 8). The
     real reason. Bad example: 'it reaches end-of-life'. Good example: 'it
     reaches end-of-life and its support is problematic due to the following
     problems: X, Y, Z'.
   - Follow 50/72 rule (see rules 2 and 7). 50 chars length for the title is
     not a strict requirement, but a good soft limit. 72 chars for the commit
     body is the strict requirement for a prose text, but it can be overrun for
     listings such as code or logs.
4. Look at the code around and mimic the style. If in doubt, consult with the
   style guides for [C][c-style] and [Lua][lua-style].
5. Don't hesitate to ask for help. If you have doubts, highlight them for a
   reviewer in the commit message, the pull request description or pull request
   comments.
6. Wait for CI and fix all problems.
   - Glance on the [checkpatch][checkpatch] output if the CI job is red.
   - If there are test fails that look irrelevant to the changes, highlight
     this fact in the pull request comments.

Once you received a review, follow the next suggestions to make the process
comfortable for everyone:

1. Don't add fixup commits on top of the initial patchset. Squash fixups into
   appropriate commits and force-push your branch. Your patchset will land to
   `master` as is, without any squashing or reformatting. Keep it in a good
   shape.
2. React to comments and respond with a summary of changes. If you disagree with
   a comment, describe your arguments or doubts.
3. Work in iterations. Either process all comments at once or mark a pull
   request as a draft and return it back when all comments will be answered. A
   reviewer always wants to just look and say 'everything is nice' rather than
   request changes and remind about forgotten things.
4. If you reached out of spare time, mark the pull request as draft or close
   it.

## Useful links

* [Good first issues][good-first-issues] -- if you search for a task to learn
  Tarantool's code.
* [How to self-review][self-review] -- more detailed recommendations how to
  make nice patches.
* [How to get involved][get-involved] -- what else goes on around Tarantool and
  how to get involved.

[ideas]: https://github.com/tarantool/tarantool/discussions/categories/ideas
[q-a]: https://github.com/tarantool/tarantool/discussions/categories/q-a
[changelogs]: doc/changelogs.md
[release-notes]: https://www.tarantool.io/en/doc/latest/contributing/release_notes/
[docbot]: https://github.com/tarantool/docbot
[commit-message]: https://www.tarantool.io/en/doc/latest/dev_guide/developer_guidelines/#how-to-write-a-commit-message
[c-style]: https://www.tarantool.io/en/doc/latest/dev_guide/c_style_guide/
[lua-style]: https://www.tarantool.io/en/doc/latest/dev_guide/lua_style_guide/
[checkpatch]: https://github.com/tarantool/checkpatch
[good-first-issues]: https://github.com/tarantool/tarantool/wiki/Good-first-issues
[self-review]: https://github.com/tarantool/tarantool/wiki/Code-review-procedure
[get-involved]: https://www.tarantool.io/en/doc/latest/contributing/contributing/
