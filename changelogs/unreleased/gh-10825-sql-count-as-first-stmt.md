## bugfix/sql

- Fixed a bug when an SQL count statement wasn't tracked by MVCC if it was
  the first in a transaction (gh-10825).
