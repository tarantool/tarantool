## feature/tools

* `tarantoolctl` has been removed. Systemd, sysvinit and logrotate
  scripts based on it were also removed. All this functionality is covered by the `tt` utility.
  `tarantoolctl` is no longer available in official deb and rpm packages. This change will only
  affect the absence of tarantoolctl in future releases of tarball archives.
  (gh-9443).
