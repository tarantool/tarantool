## bugfix/http client

* Enable runtime autodetection of system CA certificates back (gh-7372).

  Otherwise HTTPS can't be used without `verify_peer = false` option. It is the
  regression from 2.10.0.
