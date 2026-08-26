## feature/sql

* Now an error is thrown when a query uses a non-existent collation, even
  in positions where the collation would otherwise be ignored, for example
  `SELECT 'x' COLLATE nosuch` (gh-13235).
