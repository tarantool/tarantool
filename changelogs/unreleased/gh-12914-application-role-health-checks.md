## feature/config

* Application role health checks may now return a map of named check results.
  Each result is reported as a separate readiness check and may have an
  independent status, reason, and alert code (gh-12914).
