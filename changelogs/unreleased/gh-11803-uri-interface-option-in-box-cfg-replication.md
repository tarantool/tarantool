## feature/box

* Now the `interface` option is available to be used in `box.cfg.replication`
  URIs. The option allows to specify the interface to bind the connections to
  a URI to. The `default_interface` option can be used to specify the default
  `interface` value of a URI in case if multiple URIs specified (gh-11803).
