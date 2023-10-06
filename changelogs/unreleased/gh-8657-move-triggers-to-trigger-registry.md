## feature/lua

* **[Breaking change]** Triggers from `space_object`, `box.session`, and
  `box.ctl` were moved to the trigger registry (gh-8657). Now triggers set with
  `space_object:on_replace()` or `space_object:before_replace()` are attached to
  the space id, not to `space_object`. So, if you delete a space with registered
  triggers and create another space with the same id, it will use the remaining
  triggers.
