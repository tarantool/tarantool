## feature/lua/http client

* Performed automated percent-encoding of query params passed into URI using a
  using the params option. Parameters encoded using `uri.QUERY_PART` when `GET`,
  `DELETE` or `HEAD` HTTP methods are used and `uri.FORM_URLENCODED` in other
  cases (gh-7931).
