## bugfix/lib

* Fixed `http.client` to properly parse HTTP status header such as `HTTP/2 200`
  when the HTTP version does not have a minor part (gh-7319).
