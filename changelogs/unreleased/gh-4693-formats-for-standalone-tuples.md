## feature/box

* Introduced `box.tuple.format` that enables format definition for tuples
  created via `box.tuple.new` (standalone tuples) (gh-4693).
* **[Breaking change]** Disabled argument list syntax of `box.tuple.new` (this
  was needed for gh-4693). It is possible to switch to the old behavior using
  the compatibility option `box_tuple_new_vararg`.
