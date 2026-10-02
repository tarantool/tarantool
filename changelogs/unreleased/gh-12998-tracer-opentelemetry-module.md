## feature/lua

* Added a new `tracer_opentelemetry` module exposing
  `call_span(name, func, ...)`, which wraps a function call in an
  OpenTelemetry span running on its own fiber. When
  `box.cfg.enable_tracing` is `true` (gh-12998).
