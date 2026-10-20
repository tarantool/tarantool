## feature/config

* A configuration reload that changes the URIs of the replica set instances
  (for example, their SSL certificates) now applies `iproto.listen` at once and
  reconnects the replication with the new URIs after `config.reload_jitter`
  or, if it is zero, after the remote configuration storage request timeout,
  so that the other instances have time to apply the new configuration
  (gh-13172).
* Added the `iproto.drop_stale_connections` option to drop the incoming
  connections accepted on the URIs that are no longer listened on (for example,
  with the old SSL certificates) once the replication is reconnected after
  a configuration reload (gh-13172).
