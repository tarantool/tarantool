
# WebAssembly support

Tarantool provides **experimental WebAssembly support** via the `luawasm` module.
It allows you to load a compiled component, run it inside its own Wasm thread,
and call its exported functions directly from Lua.

## Loading a component

A WebAssembly component can be loaded programmatically from Lua:

```lua
local wasm = require('luawasm')

local hello = wasm:new{
  wasm = '/opt/wasm/hello.wasm',
  config = {
    stdio = {
      stdout_path = "/opt/wasm/out.txt"
    },
  },
}

hello:run()
hello:join()
````

If the `wasm` library is not available,
`require('luawasm')` raises a clear error message.

## Configuration

WebAssembly components can also be declared in a Tarantool **YAML configuration file**.
Each component is automatically registered under `box.wasm.components`
and accessible via `box.wasm.get(name)`.

A complete example is provided in:
[`doc/examples/config/wasm.yaml`](examples/config/wasm.yaml)

## Example component

Below is an example WebAssembly world definition and its implementation.

### WIT interface

```wit
package example:hello@0.1.0;

world hello {
  // Optional WASI run interface (required for fiber execution)
  export wasi:cli/run@0.2.3;

  export say-hello: func(name: string) -> string;
}
```

### Python implementation

```py
from wit_world import exports
from os import getenv

class Run(exports.Run):
    def run(self) -> None:
        name = getenv("GREETING", "unknown")
        print(f"Hello, {name}! From run.")

class WitWorld:
    def say_hello(self, name: str) -> str:
        return f"Hello, {name}! From exported function."
```

### YAML configuration

```yaml
credentials:
  users:
    guest:
      roles: [super]

groups:
  group-001:
    replicasets:
      replicaset-001:
        instances:
          instance-001: {}

wasm:
  components:
    hello:
      path: ./hello.wasm
      autorun: true
      env:
        vars:
          GREETING: 'Tarantool'
      stdio:
        stdout_path: ./hello.log
```

---

## Running with Tarantool

Start Tarantool with the configuration file:

```sh
$ tarantool --name instance-001 --config wasm.yaml -i
```

### Interactive usage

```lua
tarantool> hello = box.wasm.get("hello")
---
...

tarantool> hello:help()
Exported functions:

say_hello(name: string) -> string
---
...

tarantool> hello:say_hello("Alice")
---
- Hello, Alice! From exported function.
...

tarantool> hello:join()
---
- true
...
tarantool> \q
```

Check the output written by the Wasm component:

```sh
$ cat hello.log
Hello, Tarantool! From run.
```

## Manual usage without config

You can also manage components entirely from Lua:

```sh
$ tarantool -i
```

```lua
tarantool> luawasm = require('luawasm')
---
...

tarantool> hello = luawasm:new{
  wasm = "./hello.wasm",
  config = {
    env = { vars = { GREETING = "Tarantool" } },
    stdio = { stdout_path = "hello.log" }
  }
}
---
...

tarantool> hello:run()
---
...

tarantool> hello:join()
---
- true
...

tarantool> hello:say_hello("Alice")
---
- Hello, Alice! From exported function.
...

tarantool> \q
```

```sh
$ cat hello.log
Hello, Tarantool! From run.
```

---

## See also

* [tarawasm](https://github.com/mandesero/tarawasm) - A CLI tool for building and managing
WebAssembly components written in guest languages. Currently supports Python, Go, JavaScript, C/C++, and Rust.
* [tarantool-wit](https://github.com/mandesero/tarantool-wit) - Interface definitions (WIT) for exposing
Tarantool APIs to WASM modules.
* [tarantool-wasm-samples](https://github.com/mandesero/tarantool-wasm-samples) - Examples demonstrating
how to use Tarantool with WASM.
