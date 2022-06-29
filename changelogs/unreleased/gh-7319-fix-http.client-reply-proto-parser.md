## bugfix/lib

* Fixed http.client to properly parse HTTP status header like 'HTTP/2 200' where
  HTTP version does not have minor part (gh-7319).
