## feature/config

* A configuration reload that changes the URIs of the replica set instances
  (for example, their SSL certificates) now applies `iproto.listen` at once and
  reconnects the replication with the new URIs after `config.reload_jitter`
  or, if it is zero, after the remote configuration storage request timeout,
  so that the other instances have time to apply the new configuration
  (gh-13172).
