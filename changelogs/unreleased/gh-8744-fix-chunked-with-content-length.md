## bugfix/lua/http client

* Fixed the `Transfer-Encoding: chunked` setting being enabled even if
  the `Content-Length` header exists for stream requests (gh-8744).
