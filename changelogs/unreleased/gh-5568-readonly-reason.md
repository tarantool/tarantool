## feature/core

* Error objects with the code `box.error.READONLY` now have additional fields
  explaining why the error happened.

  Also, there is a new field `box.info.ro_reason`. It is `nil` on a writable
  instance, but reports a reason when `box.info.ro` is true (gh-5568).
