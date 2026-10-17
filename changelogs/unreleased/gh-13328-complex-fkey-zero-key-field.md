## bugfix/box

* Fixed the NULL check of a complex foreign key on delete: it compared a raw
  MsgPack byte with the `MP_NIL` type constant, so a key field equal to `0`
  was mistaken for NULL and the referential check of the deleted tuple was
  skipped, while a genuine NULL field made the delete fail with
  `wrong key type` (gh-13328).
