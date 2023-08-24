## feature/space

* Introduces the fully temporary space type. It is the same as data-temporary
  but also has temporary metadata. Temporary spaces can now be created in
  read_only mode, they disappear after server restart and don't exist on
  replicas (gh-8323).
