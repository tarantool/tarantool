## bugfix/box

* Fixed pipelined requests over the SSL transport that could wait for their
  responses until the client sent another request on the same connection
  (gh-13803).
