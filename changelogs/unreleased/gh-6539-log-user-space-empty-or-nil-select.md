## feature/box

* The behavior of empty or nil `select` calls on user spaces was changed.
  A critical log entry containing the current stack traceback is created upon
  such function calls. The user can explicitly request a full scan though by
  passing `fullscan=true` to `select`'s `options` table argument, in which case
  a log entry will not be created (gh-6539).
