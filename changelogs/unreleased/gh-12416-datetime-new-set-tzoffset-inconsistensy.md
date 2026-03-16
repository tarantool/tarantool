## bugfix/datetime

* Fixed inconsistency between dates produced by `new({tzoffset=x})`
  and `d:set({tzoffset=x})` where `d.tz ~= ''` comes before `set()`
  (gh-7680, gh-12416).
