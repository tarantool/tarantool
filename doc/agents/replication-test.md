# Testing of replication

This document explains more details about testing the replication code in
Tarantool. It gives more context about testing when developing or reviewing
patches related to the replication in any way.

## Overview

The replication tests are almost entirely located in `test/replication/` and
`test/replication-luatest/`.

Tests there usually start a replicaset with 1 or more instances, using
`luatest.replica_set` Luatest module for managing multiple instances at once, or
`luatest.server` for managing them one by one.

The tests by their very nature are probably the most flaky in the whole test
suite of Tarantool, because they involve network communication between nodes
with many different timeouts, leading to disconnects, reconnects, and races.

Such tests need to be very robust. It is very important, that if a test needs to
cover a sequence of certain events, then it must explicitly find a way to
observe these events, and make sure they don't run through without us observing
them one by one.

This means being creative with error injections such as `ERRINJ_WAL_DELAY` and
`ERRINJ_WAL_DELAY_COUNTDOWN` (the most frequently used ones) and careful with
timeouts (set huge timeouts for things which aren't expected to fail - even
5 sec is too small, 2 minutes is usually enough).

Tests must be fast. Each test ideally must run in sub-second time. Longer
duration needs to be very justified.

The tests must never assume that something is guaranteed to happen in N seconds.
All waiting must be in loops, retrying with `t.helpers.retrying(...)`.

The tests must never assume that something happens in N WAL writes. This is a
very frequent case in replication tests. Instead, let WAL writes through one by
one, checking the needed state after each of them. For example, do not assume
that Raft election will write new term -> new vote -> `PROMOTE` exactly like
this in single journal entries and in this order. Instead, block WAL writes and
limbo transactions one by one, checking the needed state after each step.

---

If anything in this document seems outdated from how the code actually works,
then it must be immediately flagged to the user.
