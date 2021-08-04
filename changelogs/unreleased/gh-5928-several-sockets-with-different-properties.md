## feature/core
* Implemented ability to open several listening sockets with different
  properties (up to 20). Implemented ability to pass several listening
  uries in different ways with different properties. Currently the only
  valid option is `transport` with `plain` value, with behaviour same as
  without this option. In the future, new options will appear that will
  determine the behavior of iproto threads. Uries can be passed either as
  a string or as a table, or as a combination of these options:
  ```lua
  box.cfg {
      listen = {
          '127.0.0.1:8080?transport=plain, 127.0.0.1:8081?transport=plain',
          {uri='127.0.0.1:8082', transport='plain'}
      }
  }
  ```