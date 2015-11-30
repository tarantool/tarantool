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

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> json=require('json')
        ---
        ...
        tarantool> json.encode(123)
        ---
        - '123'
        ...
        tarantool> json.encode({123})
        ---
        - '[123]'
        ...
        tarantool> json.encode({123, 234, 345})
        ---
        - '[123,234,345]'
        ...
        tarantool> json.encode({abc = 234, cde = 345})
        ---
        - '{"cde":345,"abc":234}'
        ...
        tarantool> json.encode({hello = {'world'}})
        ---
        - '{"hello":["world"]}'
        ...

.. function:: decode(string)

    Convert a JSON string to a Lua object.

    :param string string: a string formatted as JSON.
    :return: the original contents formatted as a Lua table.
    :rtype: table

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> json = require('json')
        ---
        ...
        tarantool> json.decode('123')
        ---
        - 123
        ...
        tarantool> json.decode('[123, "hello"]')
        ---
        - [123, 'hello']
        ...
        tarantool> json.decode('{"hello": "world"}').hello
        ---
        - world
        ...

.. data:: NULL

    A value comparable to Lua "nil" which may be useful as a placeholder in a tuple.

    **Example:**

    .. code-block:: tarantoolsession

        -- When nil is assigned to a Lua-table field, the field is null
        tarantool> {nil, 'a', 'b'}
        ---
        - - null
          - a
          - b
        ...
        -- When json.NULL is assigned to a Lua-table field, the field is json.NULL
        tarantool> {json.NULL, 'a', 'b'}
        ---
        - - null
          - a
          - b
        ...
        -- When json.NULL is assigned to a JSON field, the field is null
        tarantool> json.encode({field2 = json.NULL, field1 = 'a', field3 = 'c'}
        ---
        - '{"field2":null,"field1":"a","field3":"c"}'
        ...

The JSON output structure can be specified with ``__serialize``:

* ``__serialize="seq"`` for an array
* ``__serialize="map"`` for a map

Serializing 'A' and 'B' with different ``__serialize`` values causes different results:

.. code-block:: tarantoolsession

    tarantool> json.encode(setmetatable({'A', 'B'}, { __serialize="seq"}))
    ---
    - '["A","B"]'
    ...
    tarantool> json.encode(setmetatable({'A', 'B'}, { __serialize="map"}))
    ---
    - '{"1":"A","2":"B"}'
    ...
    tarantool> json.encode({setmetatable({f1 = 'A', f2 = 'B'}, { __serialize="map"})})
    ---
    - '[{"f2":"B","f1":"A"}]'
    ...
    tarantool> json.encode({setmetatable({f1 = 'A', f2 = 'B'}, { __serialize="seq"})})
    ---
    - '[[]]'
    ...

.. _Lua-CJSON package by Mark Pulford: http://www.kyne.com.au/~mark/software/lua-cjson.php
.. _the official documentation: http://www.kyne.com.au/~mark/software/lua-cjson-manual.html
