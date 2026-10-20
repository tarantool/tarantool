## bugfix/iproto

* Fixed IPROTO requests being rejected when they contain an unassigned key
  with a value of any MessagePack type other than nil (gh-13282).
