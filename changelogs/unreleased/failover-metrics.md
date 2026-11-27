## feature/lua

* Added the built-in `failover` metrics group. Without an Enterprise
  coordinator source, it collects no observations. Normal database metrics
  defaults remain unchanged (`all`). Process uptime in the `info` group is
  now available before `box.cfg()`.
