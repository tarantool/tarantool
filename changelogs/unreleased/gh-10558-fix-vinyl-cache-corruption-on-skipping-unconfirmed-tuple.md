## bugfix/vinyl

* Fixed a bug when `index.select()` executed in the `read-confirmed`
  transaction isolation mode (default for read-only transactions) could corrupt
  the tuple cache by creating a chain bypassing an unconfirmed tuple. The bug
  could lead to a crash or invalid query results (gh-10558).
