-------------------------------------------------------------------------------
                                    Package `msgpack`
-------------------------------------------------------------------------------

The ``msgpack`` package takes strings in MsgPack_ format and decodes them, or takes a
series of non-MsgPack values and encodes them.

.. module:: msgpack

.. function:: encode(lua_value)

    Convert a Lua object to a MsgPack string.

    :param lua_value: either a scalar value or a Lua table value.
    :return: the original value reformatted as a MsgPack string.
    :rtype: string

.. function:: decode(string)

    Convert a MsgPack string to a Lua object.

    :param string: a string formatted as YAML.
    :return: the original contents formatted as a Lua table.
    :rtype: table

.. _msgpack-null:

.. data:: NULL

    A value comparable to Lua "nil" which may be useful as a placeholder in a tuple.

=================================================
                    Example
=================================================

| :codenormal:`tarantool>` :codebold:`msgpack = require('msgpack')`
| :codenormal:`---`
| :codenormal:`...`
| :codenormal:`tarantool>` :codebold:`y =  msgpack.encode({'a',1,'b',2})`
| :codenormal:`---`
| :codenormal:`...`
| :codenormal:`tarantool>` :codebold:`tarantool>  z = msgpack.decode(y)`
| :codenormal:`---`
| :codenormal:`...`
| :codenormal:`tarantool>` :codebold:`z[1],z[2],z[3],z[4]`
| :codenormal:`---`
| :codenormal:`- a`
| :codenormal:`- 1`
| :codenormal:`- b`
| :codenormal:`- 2`
| :codenormal:`...`
| :codenormal:`tarantool>` :codebold:`box.space.tester:insert{20,msgpack.NULL,20}`
| :codenormal:`---`
| :codenormal:`- [20, null, 20]`
| :codenormal:`...`

.. _MsgPack: http://msgpack.org/
