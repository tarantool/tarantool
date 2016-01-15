-------------------------------------------------------------------------------
                                    Package `msgpack`
-------------------------------------------------------------------------------

The ``msgpack`` package takes strings in MsgPack_ format and decodes them, or
takes a series of non-MsgPack values and encodes them.

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

.. data:: NULL

    A value comparable to Lua "nil" which may be useful as a placeholder in a tuple.

=================================================
                    Example
=================================================

.. code-block:: tarantoolsession

    tarantool> msgpack = require('msgpack')
    ---
    ...
    tarantool> y = msgpack.encode({'a',1,'b',2})
    ---
    ...
    tarantool> z = msgpack.decode(y)
    ---
    ...
    tarantool> z[1], z[2], z[3], z[4]
    ---
    - a
    - 1
    - b
    - 2
    ...
    tarantool> box.space.tester:insert{20, msgpack.NULL, 20}
    ---
    - [20, null, 20]
    ...

The MsgPack output structure can be specified with ``__serialize``: |br|
* ``__serialize = "seq" or "sequence"`` for an array |br|
* ``__serialize = "map" or "mapping"`` for a map |br|
Serializing 'A' and 'B' with different ``__serialize`` values causes different results.
To show this, here is a routine which encodes
{'A','B'} both as an array and as a map, then
displays each result in hexadecimal. |br|
|nbsp| |nbsp| :codenormal:`msgpack = require('msgpack')` |br|
|nbsp| |nbsp| :codenormal:`m=msgpack.encode(setmetatable({'A', 'B'},` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ __serialize="seq"}))` |br|
|nbsp| |nbsp| :codenormal:`o1 = ''` |br|
|nbsp| |nbsp| :codenormal:`for i=1,string.len(m),1 do` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`o1=o1..string.format("%x",string.byte(m,i))..' '` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end` |br|
|nbsp| |nbsp| :codenormal:`m = msgpack.encode(setmetatable({'A', 'B'},` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ __serialize="map"}))` |br|
|nbsp| |nbsp| :codenormal:`o2 = ''` |br|
|nbsp| |nbsp| :codenormal:`for i=1,string.len(m),1 do` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`o2=o2..string.format("%x",string.byte(m,i))..' '` |br|
|nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`end` |br|
|nbsp| |nbsp| :codenormal:`print('array encoding:',o1)` |br|
|nbsp| |nbsp| :codenormal:`print('map encoding:  ',o2)` |br|
Result: |br|
|nbsp| |nbsp| :codenormal:`array encoding: 92 a1 41 a1 42` |br|
|nbsp| |nbsp| :codenormal:`map encoding:   82 1 a1 41 2 a1 42` |br|
The MsgPack Specification_ page explains that
the first encoding means |br|
fixarray(2), fixstr(1),"A",fixstr(1),"B" |br|
and the second encoding means |br|
fixmap(2), key(1), fixstr(1),"A",key(2),fixstr(2),"B".

.. _MsgPack: http://msgpack.org/
.. _Specification: http://github.com/msgpack/msgpack/blob/master/spec.md