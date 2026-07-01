## feature/box

* Introduced a recovery point manager on `box.backup.recovery_point`:
  `manager_create()`, `manager_drop()` and the `managers` table. A manager
  periodically creates recovery points using a pluggable backend and is
  available before `box.cfg()` (gh-12865).
