## feature/core

* Implemented a timeout for iproto transactions after
  which they are rolled back (gh-6177).
  Implemented new `IPROTO_TIMEOUT 0x56` key, which is
  used to set a timeout for transactions over iproto
  streams. It is stored in the body of 'IPROTO_BEGIN'
  request.
