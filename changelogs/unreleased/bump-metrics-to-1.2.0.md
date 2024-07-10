## feature/lua

* Deprecated using `cdata` values with `metrics` module `histogram:observe`.
* Updated memtx metrics descriptions from `metrics` module to be consistent.
* Added new metrics to `metrics` module: `tnt_memtx_tuples_data_total`,
  `tnt_memtx_tuples_data_read_view`, `tnt_memtx_tuples_data_garbage`,
  `tnt_memtx_index_total`, `tnt_memtx_index_read_view`, `tnt_vinyl_memory_tuple`,
  `tnt_config_alerts`, `tnt_config_status`.
