## bugfix/config

* The `experimental.connpool` module now closes cached connections and
  re-establishes them on configuration reload if the peer URI of an
  instance has been changed, including credentials and SSL parameters
  such as certificate paths (part of tarantool/tarantool-ee#1660).
