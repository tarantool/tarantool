## bugfix/raft

* Fixed several crashes and/or undefined behaviors (assertions in debug build)
  which could appear when new synchronous transactions were made during ongoing
  elections (gh-6842).
