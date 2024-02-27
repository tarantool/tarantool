## bugfix/box

* Now all triggers either have a direct impact on the execution flow (for
  example, closing a connection or throwing an error) or print a message to
  the error log when they throw an error (gh-9309).
