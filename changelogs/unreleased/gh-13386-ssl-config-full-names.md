## feature/config

* The `iproto.ssl` and `failover.iproto.ssl` declarative config sections now
  use the full SSL parameter names `ssl_ca_file`, `ssl_cert_file`,
  `ssl_key_file` (matching the URI-form names used in `iproto.listen`,
  `iproto.advertise`, and net.box). The previous truncated names `ca_file`,
  `ssl_cert`, `ssl_key` are deprecated: accepted in Tarantool 3.x with
  a validation warning, and they will be disabled in Tarantool 4.x,
  where using them is a configuration error. Use the full names in new
  and existing configurations (gh-13386).
