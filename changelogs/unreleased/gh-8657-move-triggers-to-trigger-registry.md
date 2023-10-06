## feature/lua

* **[Breaking change]** Triggers from `space_object` and `box.session` were
  moved to the trigger registry (gh-8657). Now triggers set with
  `space:on_replace()` or `space:before_replace()` are attached to the space id,
  not the space object. So, if you delete a space with registered triggers and
  create another space with the same id, it will use the triggers that are left
  from the deleted space.
