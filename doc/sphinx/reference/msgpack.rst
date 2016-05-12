.. _package-msgpack:

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

The MsgPack output structure can be specified with ``__serialize``:

* ``__serialize = "seq" or "sequence"`` for an array
* ``__serialize = "map" or "mapping"`` for a map

Serializing 'A' and 'B' with different ``__serialize`` values causes different
results. To show this, here is a routine which encodes `{'A','B'}` both as an
array and as a map, then displays each result in hexadecimal.

.. code-block:: lua

    function hexdump(bytes)
        local result = ''
        for i = 1, #bytes do
            result = result .. string.format("%x", string.byte(bytes, i)) .. ' '
        end
        return result
    end

    msgpack = require('msgpack')
    m1 = msgpack.encode(setmetatable({'A', 'B'}, {
                                 __serialize = "seq"
                              }))
    m2 = msgpack.encode(setmetatable({'A', 'B'}, {
                                 __serialize = "map"
                              }))
    print('array encoding: ', hexdump(m1))
    print('map encoding: ', hexdump(m2))

**Result:**

.. cssclass:: highlight
.. parsed-literal::

    **array** encoding: 92 a1 41 a1 42
    **map** encoding:   82 1 a1 41 2 a1 42

The MsgPack Specification_ page explains that the first encoding means:

.. cssclass:: highlight
.. parsed-literal::

    fixarray(2), fixstr(1), "A", fixstr(1), "B"

and the second encoding means:

.. cssclass:: highlight
.. parsed-literal::

    fixmap(2), key(1), fixstr(1), "A", key(2), fixstr(2), "B".

Here are examples for all the common types,
with the Lua-table representation on the left,
with the MsgPack format name and encoding on the right.

.. _common-types-and-msgpack-encodings:

    .. container:: table

        **Common Types and MsgPack Encodings**

        .. rst-class:: left-align-column-1
        .. rst-class:: left-align-column-2

        +--------------+-------------------------------------------------+
        | {}           | 'fixmap' if metatable is 'map' = 80             |
        |              | otherwise 'fixarray' = 90                       |
        +--------------+-------------------------------------------------+
        | 'a'          | 'fixstr' = a1 61                                |
        +--------------+-------------------------------------------------+
        | false        | 'false' = c2                                    |
        +--------------+-------------------------------------------------+
        | true         | 'true' = c3                                     |
        +--------------+-------------------------------------------------+
        | 127          | 'positive fixint' = 7f                          |
        +--------------+-------------------------------------------------+
        | 65535        | 'uint 16' = cd ff ff                            |
        +--------------+-------------------------------------------------+
        | 4294967295   | 'uint 32' = ce ff ff ff ff                      |
        +--------------+-------------------------------------------------+
        | nil          | 'nil' = c0                                      |
        +--------------+-------------------------------------------------+
        | msgpack.NULL | same as nil                                     |
        +--------------+-------------------------------------------------+
        | [0] = 5      | 'fixmap(1)' + 'positive fixint' (for the key)   |
        |              | + 'positive fixint' (for the value) = 81 00 05  |
        +--------------+-------------------------------------------------+
        | [0] = nil    | 'fixmap(0)' = 80 -- nil is not stored           |
        |              | when it is a missing map value                  |
        +--------------+-------------------------------------------------+
        | 1.5          | 'float 64' = cb 3f f8 0 0 0 0 0 0               |
        +--------------+-------------------------------------------------+

Also, some MsgPack configuration settings for encoding can be changed, in the
same way that they can be changed for :ref:`JSON <package-json-cfg>`.

.. _MsgPack: http://msgpack.org/
.. _Specification: http://github.com/msgpack/msgpack/blob/master/spec.md
