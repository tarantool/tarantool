# Development setup

The document explains how to set up Tarantool's sources for development: coding,
building, running tests, installing or finding dependencies.

The setup is very individual: some people prefer building in the sources, some
make a `build/` folder, some usually compile in Debug while others in
RelWithDebInfo, some will run tests using raw `luatest`, others would use
`test-run.py`.

The user must be questioned about their local setup, their preferences, their
ways of development.

The questions need to enable you to do the things explained below, unless the
user explicitly opts out some of them and wants to do certain things manually.

## Build

Tarantool uses CMake build system. Usually it is convenient to create a separate
`build` folder somewhere (very individual preference) and configure + compile
the sources in there. Having all the artifacts in one folder allows to delete it
easily if something goes wrong.

The supported CMake options are all the usual ones, like
`-DCMAKE_BUILD_TYPE=Debug` or `=RelWithDebInfo`.

Once cmake is configured, you must avoid reconfiguration when possible, because
it is expensive.

Tarantool has external dependencies - for example, it needs a compiler at the
very least. Also many other libraries like `libicu`, `libreadline`, and more.
Their installation is very individual. Developers are using various Linux
distributions with different package managers. There is no one standard way of
installing everything.

## Tests

The ways of testing depend on task and its scope. But in general all or almost
all tests can be run using `test-run.py`, from inside of the `./test` folder.

An example of usage is:
```Bash
python3 test-run.py --var <artifacts_dir> --builddir <build_dir> <param_i>...
```

The `--var` argument is optional (unless several test-run invocations are
working in parallel). But it might be convenient to control, if you want to see
and analyze the artifacts in specific easily reachable location. It can also
help to reduce the path length to the artifacts, which can be important because
the servers in the tests will use Unix sockets. Their absolute path is limited
by approximately 107 chars on Linux and 103 chars on Mac OS.

The `--builddir` is optional, but it is necessary in case of the out-of-source
build. It points to the directory where Tarantool sources were cmake-configured
and compiled (from the **Build** step, for example).

The `<param_i>...` is what the test runner will be looking for in the folder and
file names recursively in `./test`, and will run all the tests whose path
contains at least one of the positional params. For example, 2 params
`replication box` will run all tests whose file path names contain `box` or
`replication`. Such as `./box-luatest/*`, `./replication/*`,
`./replication-luatest/*` and so on.

But some users might want to run certain tests in different ways.

## Static analysis

All patches in Tarantool are subjects for static code analysis which is strictly
enforced in CI.

The local cheap must-have checks are `checkpatch` and `luacheck` tools.

Installation of these tools is highly individual. User needs to be asked about
that, and might even opt out and decide to do it manually.

Otherwise, when these tools are available (user will tell how to run them then),
they can be run as follows:
```Bash
perl -X -- checkpatch.pl --color=always --git <commit>
luacheck --codes --config .luacheckrc <file_i>...
```

`Checkpatch` is a Perl tool which will validate the commit message and its diff.

`Luacheck` must be used on all files having `.lua` extension. The tool via the
provided config will automatically decide which Lua files can be ignored, and
which will be validated. That includes all Lua files, including Luatest
`..._test.lua` files.

## GitHub API for reviews

Publishing reviews on GitHub (`doc/agents/review-gh.md`) has an experimental
mode which works via the private API of the GitHub web UI. Enabling it requires
a browser cookie.

Normally one wouldn't want to extract and store a cookie of their main account
anywhere, as it might be dangerous. The user can be suggested to create a bot
account and use its cookie instead.

The experimental mode unlocks certain features unavailable in the public API.
See the above mentioned md file to find which features, and list them to the
user so they can make a decision.

Walk the user through this setup when they want the mode enabled:

1. Optional but very desirable. Create a separate GitHub bot account (it is
  allowed by the GitHub ToS) or use an existing one. Instead of using the main
  account.

If creating a new bot account and the user already has a gmail, there is a
convenient way to bind the bot account to the same email as the main account.

Simply use a gmail alias like `<your-original-gmail>+bot@gmail.com`. For
example, if the original email is `claude@gmail.com`, then a new GitHub account
for the bot can be bound to `claude+bot@gmail.com`. GitHub treats them as
separate emails, but in gmail they are treated as one email.

2. Create a personal access token for that account with only the `public_repo`
  scope. It covers all the public-API work of `tools/gh-review.py`, which must
  then always run with the token in the `GH_TOKEN` environment variable when the
  bot account should be used.

3. Create a bot's browser session cookie for the private API: the `user_session`
  and `__Host-user_session_same_site` values from a logged-in browser profile,
  formatted as one `Cookie` header value. It is passed to `tools/gh-review.py`
  in the `GH_COOKIE` environment variable as a `<login>:<cookie>` pair, where
  the login is the GitHub account the cookie belongs to - store it in exactly
  this form. If the session expires, just generate a new one.

The private API routes the posted comments by the cookie's session, and there
is no reliable way to check programmatically which account a cookie belongs to.
That is why the account login is kept inside the `GH_COOKIE` value itself. On a
cookie refresh, remind the user to extract it from the same account's browser
profile and to keep the login prefix correct. When the token and the cookie
resolve to different accounts, the review falls apart in a confusing way: the
comments land into an invisible pending review of the cookie's account, while
the token's review has nothing to submit.

Strongly suggest to the user to keep the token and the cookie in a secrets
storage, for example the macOS keychain, with a plain text file only as a
fallback.

Enrich the steps with details how to do them exactly. For example,
- How exactly to create a token in GitHub.
- On Mac - how exactly to paste the cookies into the secrets storage.

Record in `doc/agents/local_setup.md`: the bot account name and the commands
fetching the token and the cookie (or their plain text in case the user asked
for this). How to use them, is described in the relevant skills.

## Local setup file

The results of your survey must be persisted in the `doc/agents/local_setup.md`
file. The file MUST be loaded when you plan to operate in this codebase not just
in read-only mode.

If the file is missing or outdated and you intend to build or update it, tell
the user explicitly.

If the local setup file seems outdated (for instance, user frequently corrects
you or asks to do some specific missing things regularly), you MUST suggest the
user to update this file.

When updating or creating the local setup file, you MUST save the updates BEFORE
doing anything else, to not lose the context later.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
