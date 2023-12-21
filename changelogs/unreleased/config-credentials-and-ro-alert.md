## bugfix/config

* Removed a warning in `config:info().alerts` that appears on startup on a
  replica if there are configured credentials to be written on the master
  (gh-8862).
