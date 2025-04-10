## feature/config

* Now, when the instance is configured with bootstrap strategy `supervised` or
  `native` and uses a supervised failover coordinator the guest user is
  automatically granted with privileges for performing the initial bootstrap
  using the `failover.execute` call.
