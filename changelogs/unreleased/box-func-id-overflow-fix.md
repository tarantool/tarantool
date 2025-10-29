## bugfix/box

* Fixed a bug when function IDs grew monotonously, resulting in an overflow
  after some amount of function modifications, even if the total number of
  functions was constant (gh-11849, gh-11851).
