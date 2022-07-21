## bugfix/core

* Fixed a hang when a synchronous request was issued from a net.box `on_connect`
  or `on_schema_reload` trigger. Now an error is raised instead (gh-5358).
