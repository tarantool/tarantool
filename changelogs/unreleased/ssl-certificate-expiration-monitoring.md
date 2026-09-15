## feature/config

* Added periodic expiration checks for SSL certificate files configured in
  `iproto.ssl.ssl_cert`. The check runs every 12 hours and immediately after
  configuration reload. It warns no later than approximately 24 hours before
  expiration and immediately if a certificate is already inside the warning
  window. The first certificate in each PEM file is checked.
