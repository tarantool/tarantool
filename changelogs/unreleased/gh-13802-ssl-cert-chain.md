## bugfix/core

* Fixed the SSL transport sending only the first certificate of
  `ssl_cert_file`. The intermediate CA certificates that follow it in the file
  are now sent too, so a peer that trusts only the root CA can verify the
  certificate (gh-13802).
