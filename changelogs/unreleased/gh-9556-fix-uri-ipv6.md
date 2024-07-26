## bugfix/lua/uri

* Fixed a bug that caused characters A-F to be unsupported in IPv6
  addresses. Changed the `uri.format` output for IPv6 to be
  encapsulated in brackets `[]` (gh-9556).
