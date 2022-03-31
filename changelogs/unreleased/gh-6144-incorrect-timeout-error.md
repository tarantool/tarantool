## bugfix/net.box

* Changed the type of the error returned by `net.box` on timeout
  from ClientError to TimedOut (gh-6144).
