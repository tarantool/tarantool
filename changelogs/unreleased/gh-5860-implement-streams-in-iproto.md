## feature/core

* Streams and interactive transactions over streams are implemented
  in iproto. Stream is associated with it's ID, which is unique within
  one connection. All requests with same not zero stream ID belongs to
  the same stream. All requests in stream processed synchronously. The
  execution of the next request will not start until the previous one is
  completed. If request has zero stream ID it does not belong to stream
  and is processed in the old way.
  In `net.box`, stream is an object above connection that has the same
  methods, but allows to execute requests sequentially. ID is generated
  on the client side automatically. If user writes his own connector and
  wants to use streams, he must transmit stream_id over iproto protocol.
  The main purpose of streams is transactions via iproto. Each stream
  can start its own transaction, so they allows multiplexing several
  transactions over one connection. There are multiple ways to begin,
  commit and rollback transaction: using appropriate stream methods, using
  `call` or `eval` methods or using `execute` method with sql transaction
  syntax. User can mix these methods, for example, start transaction using
  `stream:begin()`, and commit transaction using `stream:call('box.commit')`
  or stream:execute('COMMIT').
  If any request fails during the transaction, it will not affect the other
  requests in the transaction. If disconnect occurs when there is some active
  transaction in stream, this transaction will be rollbacked, if it does not
  have time to commit before this moment.

