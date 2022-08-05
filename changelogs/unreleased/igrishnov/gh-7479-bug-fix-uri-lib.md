## bugfix/uri

* Fixed a bug in the URI parser, because of which tarantoolctl
  failed to connect when the host name was skipped (gh-7479).
