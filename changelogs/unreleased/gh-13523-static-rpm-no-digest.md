## bugfix/build

* Fixed the installation of the RPM packages on Fedora 43 and newer, where
  rpm 6.0 refused them as "does not verify: no digest". The packages now carry
  SHA256 digests (gh-13523).
