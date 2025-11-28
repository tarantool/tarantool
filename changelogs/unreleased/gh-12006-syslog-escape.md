## bugfix/box

* Added character filtering for syslog output according to RFC 3164: only SP
  (%d32) and VCHAR (%d33-126) are allowed. Any character < 32 or > 126 is now
  escaped (gh-12006).
