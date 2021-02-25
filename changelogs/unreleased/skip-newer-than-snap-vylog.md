## bugfix/build

* Make recovering with force_recovery option delete newer than snapshot vylog
  files. So that instance can recover after incidents during checkpoint(gh-5823).
