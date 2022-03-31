## bugfix/core

* Fixed an assertion fail when passing a tuple without primary key fields
  to `before_replace` trigger. Now tuple format is checked before execution
  of `before_replace` triggers and after each one (gh-6780).
