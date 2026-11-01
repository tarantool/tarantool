## bugfix/core

* Fixed `box.cfg{listen = ...}` failing with "can't resolve uri for bind"
  for `127.0.0.1` on hosts where all non-loopback addresses are IPv6, and
  for `[::1]` on hosts without non-loopback IPv6 addresses (gh-13589).
