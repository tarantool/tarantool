## bugfix/core

* Fixed an assertion fail when passing a tuple without the primary key fields
  to a `before_replace` trigger. Now the tuple format is checked before the execution 
  of `before_replace` triggers and after each of them (gh-6780).
