## feature/core

* Streams and interactive transactions over streams are implemented
  in iproto. A stream is associated with its ID, which is unique within
  one connection. All requests with the same not zero stream ID belong to
  the same stream. All requests in the stream are processed synchronously.
  The execution of the next request will not start until the previous one is
  completed. If a request has zero stream ID, it does not belong to a stream
  and is processed in the old way.

  In `net.box`, a stream is an object above connection that has the same
  methods but allows to execute requests sequentially. ID is generated
  automatically on the client side. If users write their own connector and
  want to use streams, they must transmit stream_id over iproto protocol.
  The main purpose of streams is transactions via iproto.
  Each stream can start its own transaction, allowing multiplexing several
  transactions over one connection. There are multiple ways to begin,
  commit and rollback a transaction: using appropriate stream methods, using
  `call` or `eval` methods or using `execute` method with SQL transaction
  syntax. Users can mix these methods, for example, start a transaction using
  `stream:begin()`, and commit a transaction using `stream:call('box.commit')`
  or `stream:execute('COMMIT')`.

  If any request fails during the transaction, it will not affect the other
  requests in the transaction. If a disconnect occurs when there is some active
  transaction in the stream, this transaction will be rolled back if it
  does not have time to commit before this moment.
