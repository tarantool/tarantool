## bugfix/replication

* Fixed replicas writing corrupted xlogs when appending data to a local space
  from an `on_replace` or `before_replace` trigger on a global replicated space.
  Such xlogs were unrecoverable and caused other nodes to break replication with
  the replica (gh-8746, gh-8958).
