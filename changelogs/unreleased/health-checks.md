## feature/box

* Added `box.info.health` with liveness and readiness checks. Applications can
  register liveness probes with `box.ctl.liveness_probe()`, and roles can
  provide readiness checks through the `health_check()` role method.
