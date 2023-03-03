## bugfix/core

* **[Breaking change]** The key `box.space._schema['cluster']` is renamed to
  `'replicaset_uuid'`. That is not expected to be breaking because `_schema` is
  an internal system space, but the key was visible in public and documented
  (gh-5029).
