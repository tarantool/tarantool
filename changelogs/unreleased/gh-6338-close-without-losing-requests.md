## bugfix/core

* At the moment, when a net.box connection is closed, all requests that has
  not been sent will be discarded. This patch fixes this behavior: all requests
  queued for sending before the connection is closed are guaranteed to be sent
  (gh-6338).
