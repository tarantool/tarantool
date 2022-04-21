## bugfix/memtx

* Fixed repeatable `{in, up}sert` with _memtx_ transaction manager enabled which
  caused spurious transaction conflict (gh-7217).
