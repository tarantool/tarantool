## feature/lua/http client

* Query parameters passed into a URI using the `params` option are now
  percent-encoded automatically. Parameters are encoded with `uri.QUERY_PART`
  when `GET`, `DELETE`, or `HEAD` HTTP methods are used, and with
  `uri.FORM_URLENCODED` in other cases (gh-7931).
