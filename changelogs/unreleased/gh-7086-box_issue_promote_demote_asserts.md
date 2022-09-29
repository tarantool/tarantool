## bugfix/synchro

* Fixed assertions in debug builds and undefined behaviour in release builds
  when simultaneous elections started or another instance was promoted while
  an instance was acquiring or releasing the synchro queue (gh-7086).
