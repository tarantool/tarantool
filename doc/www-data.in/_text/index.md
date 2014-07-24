index:
    main: |
        # Introduction

        Tarantool is a NoSQL database running inside a Lua program. It
        combines the network programming power of Node.JS with data
        persistency capabilities of Redis. It's **open source**, BSD licensed.
        The latest release is Tarantool 1.6.3, published on July 20, 2014.

        # Features

        - a drop-in replacement for Lua 5.1, based on LuaJIT 2.0;
          simply use *#!/usr/bin/tarantool* instead of *#!/usr/bin/lua* in your script,
        - Lua packages for non-blocking I/O, fibers and HTTP,
        - [MsgPack](http://msgpack.org) data format and MsgPack based client-server
          protocol,
        - two data engines: 100% in-memory with optional persistence and a
          [2-level disk-based B-tree](http://sphia.org), to use with large data
          sets,
        - *secondary key* and index iterators support,
        - asynchronous master-master replication,
        - authentication and access control.

        # Example

        ``` lua
        #!/usr/bin/env tarantool

        box.cfg{{}}
        hosts = box.space.hosts
        if not hosts then
            hosts = box.schema.create_space('hosts')
            hosts:create_index('primary', {{ parts = {{1, 'STR'}} }})
        end

        local function handler(self)
            local host = self.req.peer.host
            local response = {{
                host = host;
                counter = space:inc(host);
            }}
            self:render({{ json = response }})
        end

        httpd = require('http.server')
        server = httpd.new('127.0.0.1', 8080)
        server:route({{ path = '/' }}, handler)
        server:start()
        ```

        # Learn more

        - [YCSB benchmark results](benchmark.html)
        - [FAQ](faq.html)
        - [Source repository](http://github.com/tarantool/tarantool)
        - [Tarantool Lua rocks](http://rocks.tarantool.org)
        - [1.5 web site and downloads](http://stable.tarantool.org)
