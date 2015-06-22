-----------------------------------------------------------------------------------------
                            Package `net.box` -- working with networked Tarantool peers
-----------------------------------------------------------------------------------------

The ``net.box`` package contains connectors to remote database systems. One
variant, ``box.net.sql``, is for connecting to MySQL or MariaDB or PostgreSQL —
that variant is the subject of the :ref:`SQL DBMS plugins <dbms-plugins>` appendix.
In this section the subject is the built-in variant, ``box.net``. This is for
connecting to tarantool servers via a network.

Call ``require('net.box')`` to get a ``net.box`` object, which will be called
``net_box`` for examples in this section. Call ``net_box.new()`` to connect and
get a connection object, which will be called ``conn`` for examples in this section.
Call the other ``net.box()`` routines, passing ``conn:``, to execute requests on
the remote box. Call :func:`conn:close <socket_object.close>` to disconnect.

All `net.box`` methods are fiber-safe, that is, it is safe to share and use the
same connection object across multiple concurrent fibers. In fact, it's perhaps
the best programming practice with Tarantool. When multiple fibers use the same
connection, all requests are pipelined through the same network socket, but each
fiber gets back a correct response. Reducing the number of active sockets lowers
the overhead of system calls and increases the overall server performance. There
are, however, cases when a single connection is not enough — for example when it's
necessary to prioritize requests or to use different authentication ids.

.. module:: net_box

.. class:: conn

  conn = net_box:new(host, port [, {other parameter[s]}])

    Create a new connection. The connection is established on demand, at the
    time of the first request. It is re-established automatically after a
    disconnect. The returned ``conn`` object supports methods for making remote
    requests, such as select, update or delete.

    For the local tarantool server there is a pre-created always-established
    connection object named :samp:`{net_box}.self`. Its purpose is to make polymorphic
    use of the ``net_box`` API easier. Therefore :samp:`conn = {net_box}:new('localhost', 3301)`
    can be replaced by :samp:`conn = {net_box}.self`. However, there is an important
    difference between the embedded connection and a remote one. With the
    embedded connection, requests which do not modify data do not yield.
    When using a remote connection, any request can yield, and local database
    state may have changed by the time it returns.

    :param string host:
    :param number port:
    :param boolean wait_connect:
    :param string user:
    :param string password:
    :return: conn object
    :rtype:  userdata

 

        EXAMPLES

        | :codebold:`conn = net_box:new('localhost', 3301)`
        | :codebold:`conn = net_box:new('127.0.0.1', box.cfg.listen, {`
        | :codebold:`wait_connect = false,`
        | :codebold:`user = 'guest',`
        | :codebold:`password = ''`
        | :codebold:`})`


    .. method:: ping()

        Execute a PING command.

        :return: true on success, false on error
        :rtype:  boolean

        Example: :codebold:`net_box.self:ping()`

    .. method:: wait_connected([timeout])

        Wait for connection to be active or closed.

        :param number timeout:
        :return: true when connected, false on failure.
        :rtype:  boolean

        Example: :codebold:`net_box.self:wait_connected()`
 

    .. method:: close()

        Close a connection.

        Connection objects are garbage collected just like any other objects in Lua, so
        an explicit destruction is not mandatory. However, since close() is a system
        call, it is good programming practice to close a connection explicitly when it
        is no longer needed, to avoid lengthy stalls of the garbage collector.

        Example: :codebold:`conn:close()`

    .. method:: conn.space.<space-name>:select{field-value, ...}

        :samp:`conn.space.{space-name}:select`:code:`{...}` is the remote-call equivalent
        of the local call :samp:`box.space.{space-name}:select`:code:`{...}`. Please note
        this difference: a local :samp:`box.space.{space-name}:select`:code:`{...}` does
        not yield, but a remote :samp:`conn.space.{space-name}:select`:code:`{...}` call
        does yield, so local data may change while a remote
        :samp:`conn.space.{space-name}:select`:code:`{...}` is running.

    .. method:: conn.space.<space-name>:insert{field-value, ...}

        :samp:`conn.space.{space-name}:insert(...)` is the remote-call equivalent
        of the local call :samp:`box.space.{space-name}:insert(...)`.

    .. method:: conn.space.<space-name>:replace{field-value, ...}

        :samp:`conn.space.{space-name}:replace(...)` is the remote-call equivalent
        of the local call :samp:`box.space.space-name:replace(...)`.

    .. method:: conn.space.<space-name>:update{field-value, ...}

        :samp:`conn.space.{space-name}:update(...)` is the remote-call equivalent
        of the local call :samp:`box.space.space-name:update(...)`.

    .. method:: conn.space.<space-name>:delete{field-value, ...}

        :samp:`conn.space.{space-name}:delete(...)` is the remote-call equivalent
        of the local call :samp:`box.space.space-name:delete(...)`.

    .. method:: call(function-name [, arguments])

        :samp:`conn:call('func', '1', '2', '3')` is the remote-call equivalent of
        :samp:`func('1', '2', '3')`. That is, ``conn:call`` is a remote
        stored-procedure call.

        Example: :codebold:`conn:call('function5')`

    .. method:: timeout(timeout)

        ``timeout(...)`` is a wrapper which sets a timeout for the request that
        follows it.

        Example: :codebold:`conn:timeout(0.5).space.tester:update({1}, {{'=', 2, 15}})`

        All remote calls support execution timeouts. Using a wrapper object makes
        the remote connection API compatible with the local one, removing the need
        for a separate :codenormal:`timeout` argument, which the local version would ignore. Once
        a request is sent, it cannot be revoked from the remote server even if a
        timeout expires: the timeout expiration only aborts the wait for the remote
        server response, not the request itself.

============================================================================
                        Example showing use of most of the net.box methods
============================================================================

This example will work with the sandbox configuration described in the preface.
That is, there is a space named tester with a numeric primary key. Assume that
the database is nearly empty. Assume that the tarantool server is running on
``localhost 127.0.0.1:3301``.

    | :codenormal:`tarantool>` :codebold:`box.schema.user.grant('guest', 'read,write,execute', 'universe')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`net_box = require('net.box')!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`function example()`
    | :codenormal:`>` :codebold:`if net_box.self:ping() then`
    | :codenormal:`>` :codebold:`table.insert(ta, 'self:ping() succeeded')`
    | :codenormal:`>` :codebold:`table.insert(ta, '  (no surprise -- self connection is pre-established)')`
    | :codenormal:`>` :codebold:`end`
    | :codenormal:`>` :codebold:`if box.cfg.listen == '3301' then`
    | :codenormal:`>` :codebold:`table.insert(ta,'The local server listen address = 3301')`
    | :codenormal:`>` :codebold:`else`
    | :codenormal:`>` :codebold:`table.insert(ta, 'The local server listen address is not 3301')`
    | :codenormal:`>` :codebold:`table.insert(ta, '(  (maybe box.cfg{...listen="3301"...} was not stated)')`
    | :codenormal:`>` :codebold:`table.insert(ta, '(  (so connect will fail)')`
    | :codenormal:`>` :codebold:`end`
    | :codenormal:`>` :codebold:`conn = net_box:new('127.0.0.1', 3301)`
    | :codenormal:`>` :codebold:`conn.space.tester:delete{800}`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn delete done on tester.')`
    | :codenormal:`>` :codebold:`conn.space.tester:insert{800, 'data'}`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn insert done on tester, index 0')`
    | :codenormal:`>` :codebold:`table.insert(ta, '  primary key value = 800.')`
    | :codenormal:`>` :codebold:`wtuple = conn.space.tester:select{800}`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn select done on tester, index 0')`
    | :codenormal:`>` :codebold:`table.insert(ta, '  number of fields = ' .. #wtuple)`
    | :codenormal:`>` :codebold:`conn.space.tester:delete{800}`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn delete done on tester')`
    | :codenormal:`>` :codebold:`conn.space.tester:replace{800, 'New data', 'Extra data'}`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn:replace done on tester')`
    | :codenormal:`>` :codebold:`conn:timeout(0.5).space.tester:update({800}, {{'=', 2, 'Fld#1'}})`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn update done on tester')`
    | :codenormal:`>` :codebold:`conn:close()`
    | :codenormal:`>` :codebold:`table.insert(ta, 'conn close done')`
    | :codenormal:`>` :codebold:`end!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('')!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`ta = {}`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`example()`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`ta`
    | :codenormal:`---`
    | :codenormal:`- - self:ping() succeeded`
    | :codenormal:`- '  (no surprise -- self connection is pre-established)'`
    | :codenormal:`- The local server listen address = 3301`
    | :codenormal:`- conn delete done on tester.`
    | :codenormal:`- conn insert done on tester, index 0`
    | :codenormal:`- '  primary key value = 800.'`
    | :codenormal:`- conn select done on tester, index 0`
    | :codenormal:`- '  number of fields = 1'`
    | :codenormal:`- conn delete done on tester`
    | :codenormal:`- conn:replace done on tester`
    | :codenormal:`- conn update done on tester`
    | :codenormal:`- conn close done`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:select{800} -- Prove that the update succeeded.`
    | :codenormal:`---`
    | :codenormal:`- [800, 'Fld#1', 'Extra data']`
    | :codenormal:`...`

