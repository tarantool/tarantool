## bugfix/replication

* Fixed accepting a known foreign replicaset during connection setup, which
  could import its data during bootstrap, automatic rejoin, or replica
  registration (gh-13646).
