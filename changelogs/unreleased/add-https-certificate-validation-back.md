## bugfix/http client

* Enabled the automatic detection of system CA certificates in the runtime (gh-7372).
  It was disabled in 2.10.0, which led to the inability to use HTTPS without
  the `verify_peer = false` option.
