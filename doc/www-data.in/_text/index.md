index:
    main: |
        # Introduction

        Tarantool is an in-memory database designed to store the most volatile
        and highly accessible web content. Tarantool has been extensively used
        in production since 2009. It's **open source**, BSD licensed.

        # Features

        - [lowest CPU overhead](benchmark.html) to store or serve a
        piece of content,
        - optional Write Ahead Logging for persistency and reliability,
        - universal data access with [rich Lua stored
        procedures](http://github.com/mailru/tntlua), which can exchange messages
        between each other or networked peers,
        - asynchronous master-slave replication and hot backup.

        # Get started

        ``` bash
        # apt-get install tarantool tarantool-client
        # cd /etc/tarantool
        # cp instances.available/example.cfg instances.enabled/fqueue.cfg
        # cd /usr/share/tarantool/lua
        # wget http://github.com/mailru/tntlua/raw/master/fqueue.lua -O init.lua
        # service tarantool start
        tarantool: Staring instances
            Starting 'fqueue' ... ok
        ```

        A fast and customizable message queue server is up and running.

        # Learn more

        - [YCSB benchmark results](benchmark.html)
        - [FAQ](faq.html)
        - [Source repository](http://github.com/tarantool/tarantool)
        - [Lua stored procedures repository]( http://github.com/mailru/tntlua)
