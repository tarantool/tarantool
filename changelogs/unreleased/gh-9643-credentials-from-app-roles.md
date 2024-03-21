## bugfix/config

* A non-existent role can now be assigned in the `credential` section of
  the configuration (gh-9643).
* Privileges that were not granted by the config module are no longer
  revoked by the config module (gh-9643).
