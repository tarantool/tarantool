## bugfix/box

* Fixed a bug when object privileges could remain granted on revoke if they were
  the last ones in the `_priv` entry (gh-11528).
