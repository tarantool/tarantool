## feature/replication

* **[Breaking change]** Introduce new configuration option `bootstrap_strategy`.
  The default value of this option - "auto" - brings new replica behaviour on
  replica set bootstrap, recovery and on replication reconfiguration. One of the
  notable changes is that `replication_connect_quorum` option is not taken into
  account with `bootstrap_strategy` = "auto". If you wish to return to the old
  behaviour, you may use `bootstrap_strategy` = "legacy" (deprecated) (gh-5272).
