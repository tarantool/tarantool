## feature/compat

* The following compatibility options' defaults were switched to new behavior:
    * `yaml_pretty_multiline`
    * `sql_seq_scan_default`
    * `json_escape_forward_slash`
    * `fiber_channel_close_mode`
    * `fiber_slice_default`
    * `box_cfg_replication_sync_timeout`
    * `c_func_iproto_multireturn`

  More information on the new behavior can be found on the [Module compat](https://www.tarantool.io/en/doc/latest/reference/reference_lua/compat/) page.
