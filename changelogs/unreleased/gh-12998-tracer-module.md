## feature/lua

* A new module tracer is added. The module is invoked using
  the command `tracer = require('tracer')`. The module includes
  the following functions:

* `tracer.call_span(name, func, args)` - creates and records a span for
  the duration of the function `func`. If tracing is disabled, it simply
  executes the function.
  `name` is name of the span being created, `func` is a function that
  will be executed, `args` are arguments of function func (gh-12998).

* `tracer.enable()` - includes tracing; disabled by default (gh-12998).

* `tracer.disable()` - disables tracing (gh-12998).

* `tracer.on_span_end(callback)` - specifies a function for saving
  span information after the span completes. By default,
  span information is written to the log.
  `callback` is function for sending span information (gh-12998).
