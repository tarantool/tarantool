-------------------------------------------------------------------------------
                            Package `yaml`
-------------------------------------------------------------------------------

The ``yaml`` package takes strings in YAML_ format and decodes them, or takes a
series of non-YAML values and encodes them.

.. module:: yaml

.. function:: encode(lua_value)

    Convert a Lua object to a YAML string.

    :param lua_value: either a scalar value or a Lua table value.
    :return: the original value reformatted as a YAML string.
    :rtype: string

.. function:: decode(string)

    Convert a YAML string to a Lua object.

    :param string: a string formatted as YAML.
    :return: the original contents formatted as a Lua table.
    :rtype: table

.. data:: NULL

    A value comparable to Lua "nil" which may be useful as a placeholder in a tuple.

=================================================
                    Example
=================================================

.. code-block:: tarantoolsession

    tarantool> yaml = require('yaml')
    ---
    ...
    tarantool> y = yaml.encode({'a', 1, 'b', 2})
    ---
    ...
    tarantool> z = yaml.decode(y)
    ---
    ...
    tarantool> z[1], z[2], z[3], z[4]
    ---
    - a
    - 1
    - b
    - 2
    ...
    tarantool> if yaml.NULL == nil then print('hi') end
    hi
    ---
    ...

The `YAML collection style <http://yaml.org/spec/1.1/#id930798>`_ can be
specified with ``__serialize``:

* ``__serialize="sequence"`` for a Block Sequence array,
* ``__serialize="seq"`` for a Flow Sequence array,
* ``__serialize="mapping"`` for a Block Mapping map,
* ``__serialize="map"`` for a Flow Mapping map.

Serializing 'A' and 'B' with different ``__serialize`` values causes
different results:

.. code-block:: tarantoolsession

    tarantool> yaml = require('yaml')
    ---
    ...
    tarantool> yaml.encode(setmetatable({'A', 'B'}, { __serialize="sequence"}))
    ---
    - |
      ---
      - A
      - B
      ...
    ...
    tarantool> yaml.encode(setmetatable({'A', 'B'}, { __serialize="seq"}))
    ---
    - |
      ---
      ['A', 'B']
      ...
    ...
    tarantool> yaml.encode({setmetatable({f1 = 'A', f2 = 'B'}, { __serialize="map"})})
    ---
    - |
      ---
      - {'f2': 'B', 'f1': 'A'}
      ...
    ...
    tarantool> yaml.encode({setmetatable({f1 = 'A', f2 = 'B'}, { __serialize="mapping"})})
    ---
    - |
      ---
      - f2: B
        f1: A
      ...
    ...

.. _YAML: http://yaml.org/
