## feature/box

* Introduced a recovery point manager in `box.backup.recovery_point`:
  `manager_create()`, `manager_drop()`, and the `managers` table. A manager
  periodically creates recovery points using a pluggable backend and is
  available before `box.cfg()`  is called (gh-12865).
