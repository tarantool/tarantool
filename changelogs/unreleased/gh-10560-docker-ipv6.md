## bugfix/docker

* Fixed startup of the Docker image on IPv6-only networks. The default
  listener now binds the available IP families instead of requiring IPv4
  (gh-10560).
