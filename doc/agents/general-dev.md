# Development in Tarantool

This document explains how to write code in Tarantool, the expected standards,
approaches, style, formatting, and other rules.

## Commits

Patches for Tarantool are always organized as a sequence of atomic commits. That
is, such commits which are fully self-sufficient and make sense and bring value
on their own.

Each commit must be such that nothing can be removed from it without making it
lose its sense, and nothing needs to be added to make it valuable and working.

Each commit must compile and pass all tests.

The commit messages have a specific structure:
- Title has the format `system: imperative title`. For example,
  `limbo: reorganize the code`. It is limited by 50 chars max, unless it is
  impossible for some reason.
- The message is descriptive, not narrating the code, explaining the reason for
  the commit, perhaps some general outline of what is done if the code is highly
  complex. The message lines are limited by 72 symbols in width unless it is
  impossible (for example, a long link is inserted).

The body of the message must reference a specific GitHub ticket when work is
related to one.

Commit message must have in its end some tags:
- `NO_DOC=<reason>` - specified when the commit changes no documented behavior
  (no new public features or changes to existing ones), and thus has no
  `@TarantoolBot document` label in the message.
- `NO_CHANGELOG=<reason>` - changelogs are added to `changelogs/unreleased/`
  when anything visible for users is changed. That includes bug fixes and any
  other observable behavior changes. Add the tag when no changelog file is
  added/modified.
- `NO_TEST=<reason>` - when no tests are added or modified in the commit. It
  might normally happen for refactoring commits which change already covered
  code.

Commit message might have to add a documentation request when a feature is added
or modified or deprecated. This is done in the form of:
```
@TarantoolBot document
Title: <title ...>
Multiline description ...
```
When the commit is merged into the main branch, there will be a GitHub ticket
automatically created with the given title and message. This text is encouraged
to use markdown formatting to make the ticket look nice. When no doc request is
added, the commit message must specify `NO_DOC` tag.

The ordering of commit message sections is: title -> message -> ticket ref ->
tags -> doc request.

Commit title, message, and the diff must exclusively use ASCII characters. The
only exception allowed can be something that explicitly requires a non-ASCII
character. For example, string collations testing.

## Code

Tarantool's main language is C. This enforces very careful treatment of memory
management, but opens up great performance possibilities.

Code must be modular. That is, new functions must be added exactly to where they
belong, and ideally must be scoped inside a single .c/.cc file as `static`
functions unless we absolutely need to reuse the function in other places.
Changed functions might need to be moved to other modules if their scope
changes.

It is very important that the code remains modular, because it greatly reduces
the cognitive complexity, allowing to work on various parts of Tarantool without
having to be aware of most of the other parts.

Code must be performant. We avoid using the heap at all costs in hot code,
preferring the stack, the fiber-local and transaction-local region memory, and
intrusive heap-free containers such as `rlist` and `stailq`. We also must avoid
`if`-branching where possible and copying of all sorts.

The performance value is superseded by the huge value of simplicity only off the
hot paths, and only when chasing a small performance gain would require
disproportionately complex code. In any code that isn't hot we must at all costs
keep the code very simple. Simple short functions, linear code, good deep
comments where necessary.

Existing code is encouraged to be changed when it helps to reuse it, or when it
can be even dropped as superseded by newer code. Legacy code, when possible,
must not be kept for any sort of "backward compatibility" or "for later rework",
unless it maintains some public behavior.

Comments in the code must never be narrative. If a comment just narrates the
next lines, then it must be deleted. If a comment somehow speaks about commits
and our current development plan, then it must be deleted. If a comment is just
obvious in any way, then it must be deleted. Do not write comments at all unless
the commented code is extremely complex and external context is needed for
understanding it.

The only allowed cases of obvious comments are the ones enforced by our code
style:
- Every struct must have a comment.
- Every struct member must have a comment.
- Every inline function longer than 5 lines must have a comment.
- Every function declaration must have a comment, when the definition is
  somewhere else.
- Every global variable must have a comment.

## Style

- Text file line lengths (code, text, Markdown, etc) are limited by 80 chars.
- Naming for variables, functions, structs, aliases is `like_this`.
- Only C++ classes are named `LikeThis`.
- Function declaration arguments wrapped on more than one line must be aligned
  by the `(` char of the function. Same applies to alignment of multi-line
  `while (...)`, `for (...)`, `if (...)`.
- Indentation is tabs, 8 chars wide.
- Comments inside functions are always wrapped into `/* ... */`. If it fits
  into one line, then it is just `/*...*/`. Otherwise it is `/*` - line 1,
  ` *` - next lines, `*/` - last line. Comments outside functions are same, but
  start with `/**`.

## Tests

Each changed line, unless it is a simple refactoring like a rename or a code
move, MUST be covered by tests.

Older tests might already exist sometimes, covering this place implicitly. Or
the refactoring might be such that some tests cover the changed code naturally
and explicitly.

But much more often than not, new tests should be written.

For third-party libs, internal core libs, storage-agnostic, and other very
isolated modules written in C/C++ it is preferable to write unit tests in C.
Examples can be found in `test/unit/`.

For all other changes the preferable way of testing is Luatest. Examples can be
found in `test/*-luatest/` folders.

## Setup

Development in Tarantool is quite individual. You MUST load
`doc/agents/local_setup.md` to understand how to work in user's environment for
configuring, building, testing, static analysis.

If and only if that file is missing or seems outdated, you must load
`doc/agents/setup-dev.md` to create/update the local setup file.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
