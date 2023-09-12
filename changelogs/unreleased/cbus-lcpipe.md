## feature/build

* Exported the `cbus` interface: `cbus_endpoint_new`, `cbus_endpoint_delete`, `cbus_loop`, and `cbus_process`.
* Added a new type of `cbus` pipe: `lcpipe`. The `lctype` is used to send messages from a third-party thread to the Tarantool cord.