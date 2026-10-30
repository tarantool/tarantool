# Reviewing patches in Tarantool

This document explains how to review a Tarantool patch: which standards to
validate it against, which additional knowledge to load, and how to report
the findings.

## Required knowledge

The review standards are exactly the development standards. You MUST load
`doc/agents/general-dev.md` and validate the patch against everything stated
there: commit organization and message format, modularity, performance,
comments, code style, test coverage.

Additionally, look at the other documents in `doc/agents/` and load the ones
whose subject matches what the patch touches. For example, a patch changing
replication logic requires `replication-dev.md`, and its tests require
`replication-test.md`. Do not load documents unrelated to the patch. New
documents can appear there over time, so always check the actual directory
listing, not just the examples above.

## Reporting

Each finding in a specific commit must reference that commit and, when it is
about code, the file and the line. State the reasoning behind each comment, not
just 'do this' or 'do that'.

Split the findings into separate comments instead of presenting them as a flat
list of squashed remarks.

A review only reports findings. Do not edit the patch and do not fix the
findings, unless the user explicitly asks for that afterwards. But you can
suggest direction of the fix in the comment itself when it is clear.

Be picky and dig deep, but stay polite. Never phrase findings as commands
("Test this", "Fix this", "Restore the guard"). Phrase them as questions
or suggestions: "Is that right? Would be worth fixing then", "Would you
test this please?", "Perhaps X would work here?". Be confident about the
facts and humble about the ask.

The review summary must be short and encouraging - the inline comments carry all
the details. Acknowledge the work, optionally point at the one or two comment
types worth reading first, and stop. Do not catalog everything that is wrong
with the patch: a wall of negatives destroys the reader, and duplicates what the
comments already say.

## Review file

When a review is just getting started, suggest the user to keep the findings in
a local review file. Suggest it before every new review. The user might later
use the file as a source for posting the comments somewhere, or might prefer to
work with the findings directly from the chat.

The file is stored as `reviews/<target>.md`, where the target names the
reviewed thing: a PR number like `pr12989`, a branch name, a commit range.

The structure of the file:

- A header: what is reviewed (the PR, the branch, the commit range), its base,
  the date of starting the review.
- When the findings are being delivered somewhere (for example, posted to
  GitHub), a note explaining the progress marks: delivered findings get `DONE`
  appended to their headings, consciously dropped ones - `SKIPPED`. This way it
  is always visible where the work stopped.
- The patchset-wide findings, each as its own section.
- A section per commit, `### <short-hash> <title>`, holding that commit's
  findings. Each finding is written exactly like a ready comment (the label, the
  type, the text), and references its file and line.

## Formatting

The first line of every comment must be literally these six characters:

    `[AI]`

The backquotes are part of the comment text - they make the label render as
inline code, so it is clearly visible, and the readers know the comment is
automatically generated.

When the comment belongs to one of the types below, the label line also carries
the type's emoji and the type name in backquote quotes (lowercase, one short
word), and the comment body starts on the next line. The emoji are given here
by their name and Unicode code instead of literally, to keep this file
ASCII-only as the general guidelines demand - in the posted comments use the
actual emoji characters:

- `typo`, pencil emoji (U+270F U+FE0F) - typos.
- `nit`, light bulb emoji (U+1F4A1) - recommendations which are not
  obligatory to address.
- `crash`, collision emoji (U+1F4A5) - crashes.
- `bug`, lady beetle emoji (U+1F41E) - behavior bugs.
- `optimization`, high voltage emoji (U+26A1) - removal of unused struct
  members, performance issues anywhere, code simplification.

All other comments carry no emoji and no type word - just the label.

Split the comment body into short paragraphs when it has more than one
thought in it, instead of posting a single wall of text. The rule applies
everywhere a ready comment lives: posted to GitHub or stored in the local
review file.

An example of a full comment:

    `[AI]` <emoji> `bug`
    The check compares the wrong counter: ... <the comment body>.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
