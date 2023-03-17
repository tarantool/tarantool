## bugfix/box

* Fixed a bug when big numbers were incorrectly encoded by msgpackffi,
  that could lead to wrong select results with big number keys (gh-6119).
