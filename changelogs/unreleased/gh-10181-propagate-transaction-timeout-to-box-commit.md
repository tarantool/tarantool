## feature/box

* A new compat option `compat.box_begin_timeout_meaning` has been added.
  Controls the behavior of the timeout `box.begin{timeout=`. If set to 'new',
  the transaction timeout is propagated further to `box.commit`. If set to
  'old', the behavior is no different from what it was before this patch
  appeared (gh-10181).
