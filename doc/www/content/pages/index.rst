:title: Tarantool - A NoSQL database in a Lua Script
:slug: index
:save_as: index.html

-------------------------------------------------------------------------------
                               Introduction
-------------------------------------------------------------------------------

Tarantool is a NoSQL database running inside a Lua program. It
combines the network programming power of Node.JS with data
persistency capabilities of Redis. It's **open source**, BSD licensed.
The latest release is Tarantool 1.6.3, published on July 20, 2014.

-------------------------------------------------------------------------------
                                  Features
-------------------------------------------------------------------------------

- a drop-in replacement for Lua 5.1, based on LuaJIT 2.0; simply use \
  :code:`#!/usr/bin/tarantool` instead of :code:`#!/usr/bin/lua` in your script,
- Lua packages for non-blocking I/O, fibers and HTTP,
- `MsgPack <http://msgpack.org>`_ data format and \
  MsgPack based client-server protocol,
- two data engines: 100% in-memory with optional persistence and a \
  `2-level disk-based B-tree <http://sphia.org>`_, to use with large data sets,
- *secondary key* and index iterators support,
- asynchronous master-master replication,
- authentication and access control.

Our `online shell <http://try.tarantool.org>`_ gives a taste of these features
and is a `Tarantool Lua script <http://github.com/tarantool/try>`_.

-------------------------------------------------------------------------------
                                    News
-------------------------------------------------------------------------------

- **Meet with Tarantool developers at** `Lua Workshop 2014! <http://luaconf.ru>`_
- *2014-08-01* Tarantool 1.5.4 is released
- *2014-07-20* Tarantool 1.6.3 is released

-------------------------------------------------------------------------------
                                   Example
-------------------------------------------------------------------------------


.. code-block:: lua
    :linenos: inline

    #!/usr/bin/env tarantool

    box.cfg{}
    hosts = box.space.hosts
    if not hosts then
        hosts = box.schema.create_space('hosts')
        hosts:create_index('primary', { parts = {1, 'STR'} })
    end

    local function handler(self)
        local host = self.req.peer.host
        local response = {
            host = host;
            counter = hosts:inc(host);
        }
        self:render{ json = response }
    end

    httpd = require('http.server')
    server = httpd.new('127.0.0.1', 8080)
    server:route({ path = '/' }, handler)
    server:start()

-------------------------------------------------------------------------------
                                  Learn more
-------------------------------------------------------------------------------

- `YCSB benchmark results <benchmark.html>`_
- `1.5 web site and downloads <http://stable.tarantool.org>`_

.. raw:: html

    <style>
        #forkongithub a {
            background:#000;
            color:#fff;
            text-decoration:none;
            font-family:arial,sans-serif;
            text-align:center;
            font-weight:bold;
            padding:5px 40px;
            font-size:1rem;
            line-height:2rem;
            position:relative;
            transition:0.5s;
        }
        #forkongithub a:hover {
            background:#999;
            color:#fff;
        }
        #forkongithub a::before,
        #forkongithub a::after {
            content:"";
            width:100%;
            display:block;
            position:absolute;
            top:1px;
            left:0;
            height:1px;
            background:#fff;
        }
        #forkongithub a::after {
            bottom:1px;top:auto;
        }
        @media screen and (min-width:800px) {
            #forkongithub {
                position:absolute;
                display:block;
                top:0;
                right:0;
                width:200px;
                overflow:hidden;
                height:200px;
                z-index:9999;
            }
            #forkongithub a {
                width:200px;
                position:absolute;
                top:60px;
                right:-60px;
                transform:rotate(45deg);
                -webkit-transform:rotate(45deg);
                -ms-transform:rotate(45deg);
                -moz-transform:rotate(45deg);
                -o-transform:rotate(45deg);
                box-shadow:4px 4px 10px rgba(0,0,0,0.8);
            }
        }
    </style>
    <span id="forkongithub">
        <a href="https://github.com/tarantool/tarantool">Follow on GitHub</a>
    </span>
