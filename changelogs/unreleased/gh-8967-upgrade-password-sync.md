## feature/config

* Implemented a full password support in the `config.credentials` schema,
  including a password setting, updating and removal for the `chap-sha1`
  auth type (supported by both Tarantool Community Edition and Tarantool
  Enterprise Edition) and the `pap-sha256` (just for Enterprise Edition
  where it is available) (gh-8967).
