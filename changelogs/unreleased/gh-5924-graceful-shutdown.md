## feature/iproto

* Introduce `graceful shutdown` mechanism
* Introduce `net.box.connection:shutdown()` - send `IPROTO_SHUTDOWN` to the
  server
* Introduce `net.box.connection:set_shutdown_handler(handler)` - set handler to
  do somethink after getting `IPROTO_SHUTDOWN` from the server and sending
  `IPROTO_SHUTDOWN`  in response to the server
* Introduce new boolean value of `_session_settings` space -
  `_session_settings.graceful_shutdown`

Connection support graceful shutdown if it set
`_session_settings.graceful_shutdown = true`. By default it is `false` and
connection doesn't support graceful shutdown. This value is can be changed
only if shutdown is not started  (gh-5924).

 ### Common flow for shutdown:

 1. Instance is signaled via SIGTERM (or by `os.exit`) to shutdown.
 At that moment there could be:
     + Listen sockets, than accept new connections
     + Active opened connections
     + Some fibers, that currently processing requests (incl long requests
       with `box.session.push` scenario)
     + Some requests "in the fly", that was already sent by client into this
       connection, but yet not accepted by Tx.
     + Some responses "in the fly", that was already replied by Tx and are in
       state of sending data
 2. Server should stop accept io and close listen sockets. This will lead that
    new connections would not created, but existing ones continue to work
 3. Server should iterate over all it's open connections and:
     - if connection support shutdown, then:
         + send to it `IPROTO_SHUTDOWN` packet
     - if connection does not support shutdown, then:
         + read current socket until iproto packet boundary
         + then `shutdown(s, SHUT_RD)` this connection to prevent further reads
 4. Server should wait (within shutdown_timeout) on connections for:
     - if connection support shutdown, then:
         + wait for `IPROTO_SHUTDOWN` packet from client side
         + upon receival do `shutdown(s, SHUT_RD)` to connection.
         + watch active requests count. when it comes to `0`, close this connection
     - if connection does not support shutdown, then:
         + watch active requests count. when it comes to `0`, close this connection
     - wait while open connection count is more than `0`.
 5. CLose all open connections. When all connections closed, shutdown is done.

 Upon receival `IPROTO_SHUTDOWN` from any client server should:
 * mark connection as `support shutdown` if not yet
 * perform `shutdown(s, SHUT_RD)` on connection
