## feature/box

* Improved checking for dangerous `select` calls. The calls with
  `offset + limit <= 1000` are now considered safe, which means a warning is not
  issued. The 'ALL', 'GE', 'GT', 'LE', 'LT' iterators are now considered dangerous
  by default even with the key present (gh-7129).
