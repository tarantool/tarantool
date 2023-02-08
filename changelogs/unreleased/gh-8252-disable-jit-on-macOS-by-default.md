## bugfix/luajit

* To improve customer experience it was decided to disable JIT engine on
  Tarantool startup for macOS builds. Either way, JIT will be aboard as a
  result of the changes and more adventurous users will be able to enable
  it via `jit.on` in their code (gh-8252).
