## feature/config

* **[Breaking change]** `box.info.ro_reason` and `ER_READONLY` errors now
  report which declarative configuration mode made an instance read-only
  instead of the generic `config` reason. Applications that check
  `box.info.ro_reason == 'config'` must account for the new reasons
  (gh-10405).
