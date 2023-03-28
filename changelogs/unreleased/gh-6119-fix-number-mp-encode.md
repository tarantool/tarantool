## bugfix/box

* Fixed a bug when large numbers were encoded incorrectly by `msgpackffi`.
  It could lead to wrong select results with large number keys (gh-6119).
