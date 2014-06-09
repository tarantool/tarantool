index:
    main: |
        # Introduction

        Tarantool is a NoSQL database running inside a Lua program. It's
        created to store and process the most volatile and highly accessible
        Web data. Tarantool has been extensively used in production since 2009.
        It's **open source**, BSD licensed.

        # Features

        - a drop-in replacement for Lua 5.1, based on LuaJIT 2.0;
          simply use *#!/usr/bin/tarantool* instead of *#!/usr/bin/lua* in your script,
        - [MsgPack](http://msgpack.org) data format and MsgPack based client-server
          protocol,
        - two data engines: 100% in-memory with optional persistence and a
          [2-level disk-based B-tree](http://sphia.org), to use with large data
          sets,
        - secondary key and index iterators support,
        - asynchronous master-master replication,
        - authentication and access control,
        - [lowest CPU overhead](benchmark.html) to store or serve a
        piece of content.

        # Get started

        ``` bash
        # apt-get install tarantool
        # tarantool
        # tarantool> box.cfg{{admin_port=3313}}
        # tarantool> myspace = box.schema.create_space('myspace')
        # tarantool> myspace:create_index('primary')
        # tarantool> tuple = {{ name = 'Tarantool', release = box.info.version,
        #            type = {{ 'NoSQL database', 'Lua interpreter'}}}}
        # tarantool> myspace:auto_increment{{tuple}}
        #   - [1, {{'release': '1.6.1-445-ge8d3201', 'name': 'Tarantool'
        #          'type': ['NoSQL database', 'Lua interpreter']}}]
        ```

        # Learn more

        - [YCSB benchmark results](benchmark.html)
        - [FAQ](faq.html)
        - [Source repository](http://github.com/tarantool/tarantool)
        - [Tarantool Lua rocks](http://rocks.tarantool.org)
        - [1.5 web site and downloads](http://stable.tarantool.org)
