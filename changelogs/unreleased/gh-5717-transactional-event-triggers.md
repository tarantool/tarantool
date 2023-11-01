## feature/core

* Introduced transaction-related events `box.before_commit`, `box.on_commit`,
  and `box.on_rollback` for the new trigger registry. One of the main advantages
  of the new triggers is that they can be set for all transactions rather than
  setting them within each transaction (gh-5717, gh-8656).
