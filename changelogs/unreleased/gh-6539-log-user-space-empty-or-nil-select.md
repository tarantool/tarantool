## feature/box

* Changed behaviour of empty or nil `select` calls on user spaces: a critical
  log entry containing the current stack traceback is created upon such
  function calls — a user can explicitly request a full scan though by passing
  `fullscan=true` to `select`'s `options` table argument in which a case a
  log entry will not be created. (gh-6539).
