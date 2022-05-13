## feature/box

* Improved check for dangerous select calls: calls with `offset + limit <= 100`
  are now considered safe (i.e., a warning is not issued); 'ALL', 'GE', 'GT',
  'LE', 'LT' iterators are now considered dangerous by default even with key
  present (gh-7129).
