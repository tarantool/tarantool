## bugfix/replication

* Fixed a bug when an anonymous replica register or a replica name assignment
  could fail with an error "LSN for ... is used twice" in release and crash in
  debug (gh-9916).
