## feature/box

* Removed the warning logged by potentially long `index.select()` calls.
  The warning became useless after the introduction of the fiber slice limit.
