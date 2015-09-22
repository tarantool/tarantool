-------------------------------------------------------------------------------
                          Package `json`
-------------------------------------------------------------------------------

The json package provides JSON manipulation routines. It is based on the
`Lua-CJSON package by Mark Pulford`_. For a complete manual on Lua-CJSON please read
`the official documentation`_.

.. module:: json

.. function:: encode(lua-value)

    Convert a Lua object to a JSON string.

    :param lua_value: either a scalar value or a Lua table value.
    :return: the original value reformatted as a JSON string.
    :rtype: string

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`json=require('json')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.encode(123)`
    | :codenormal:`---`
    | :codenormal:`- '123'`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.encode({123})`
    | :codenormal:`---`
    | :codenormal:`- '[123]'`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.encode({123, 234, 345})`
    | :codenormal:`---`
    | :codenormal:`- '[123,234,345]'`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.encode({abc = 234, cde = 345})`
    | :codenormal:`---`
    | :codenormal:`- '{"cde":345,"abc":234}'`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.encode({hello = {'world'}})`
    | :codenormal:`---`
    | :codenormal:`- '{"hello":["world"]}'`
    | :codenormal:`...`

.. function:: decode(string)

    Convert a JSON string to a Lua object.

    :param string string: a string formatted as JSON.
    :return: the original contents formatted as a Lua table.
    :rtype: table

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`json=require('json')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.decode('123')`
    | :codenormal:`---`
    | :codenormal:`- 123`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.decode('[123, "hello"]')[2]`
    | :codenormal:`---`
    | :codenormal:`- hello`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`json.decode('{"hello": "world"}').hello`
    | :codenormal:`---`
    | :codenormal:`- world`
    | :codenormal:`...`

.. _json-null:

.. data:: NULL

    A value comparable to Lua "nil" which may be useful as a placeholder in a tuple.

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`-- When nil is assigned to a Lua-table field, the field is null`
    | :codenormal:`tarantool>` :codebold:`{nil, 'a', 'b'}`
    | :codenormal:`- - null`
    | :codenormal:`- a`
    | :codenormal:`- b`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`-- When json.NULL is assigned to a Lua-table field, the field is json.NULL`
    | :codenormal:`tarantool>` :codebold:`{json.NULL, 'a', 'b'}`
    | :codenormal:`---`
    | :codenormal:`- - null`
    | :codenormal:`- a`
    | :codenormal:`- b`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`-- When json.NULL is assigned to a JSON field, the field is null`
    | :codenormal:`tarantool>` :codebold:`json.encode({field2 = json.NULL, field1 = 'a',  field3 = 'c'})`
    | :codenormal:`---`
    | :codenormal:`- '{"field2":null,"field1":"a","field3":"c"}'`
    | :codenormal:`...`

The JSON output structure  can be specified with :code:`__serialize`:
__serialize="seq" for an array,
__serialize="map" for a map.
Serializing 'A' and 'B' with different __serialize values causes different results: |br|
:codebold:`json.encode(setmetatable({'A', 'B'}, { __serialize="seq"}))` |br|
:codenormal:`- '["A","B"]'` |br|
:codebold:`json.encode({setmetatable({f1 = 'A', f2 = 'B'}, { __serialize="map"})})` |br|
:codenormal:`- '[{"f2":"B","f1":"A"}]'` |br|

.. _Lua-CJSON package by Mark Pulford: http://www.kyne.com.au/~mark/software/lua-cjson.php
.. _the official documentation: http://www.kyne.com.au/~mark/software/lua-cjson-manual.html
