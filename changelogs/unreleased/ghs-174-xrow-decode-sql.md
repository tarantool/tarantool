## bugfix/iproto

* Fixed a crash caused by `IPROTO_EXECUTE` and `IPROTO_PREPARE` requests
  containing SQL fields with unexpected MessagePack types (ghs-174).
* Fixed decoding SQL request keys encoded using a non-optimal, multibyte
  MessagePack integer representation (ghs-174).
