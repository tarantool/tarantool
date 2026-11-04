## feature/config

* Report which declarative configuration mode made an instance read-only via
  `box.info.ro_details` and `ER_READONLY` error details. The existing
  `box.info.ro_reason` and error `reason` values are unchanged (gh-10405).
