-------------------------------------------------------------------------------
                            Package `pickle`
-------------------------------------------------------------------------------

.. module:: pickle

.. function:: pack(format, argument [, argument ...])

    To use Tarantool binary protocol primitives from Lua, it's necessary to
    convert Lua variables to binary format. The ``pickle.pack()`` helper
    function is prototyped after Perl 'pack_'.

    .. container:: table

        **Format specifiers**

        +------+-------------------------------------------------+
        | b, B | converts Lua variable to a 1-byte integer,      |
        |      | and stores the integer in the resulting string  |
        +------+-------------------------------------------------+
        | s, S | converts Lua variable to a 2-byte integer, and  |
        |      | stores the integer in the resulting string,     |
        |      | low byte first                                  |
        +------+-------------------------------------------------+
        | i, I | converts Lua variable to a 4-byte integer, and  |
        |      | stores the integer in the resulting string, low |
        |      | byte first                                      |
        +------+-------------------------------------------------+
        | l, L | converts Lua variable to an 8-byte integer, and |
        |      | stores the integer in the resulting string, low |
        |      | byte first                                      |
        +------+-------------------------------------------------+
        | n    | converts Lua variable to a 2-byte integer, and  |
        |      | stores the integer in the resulting string, big |
        |      | endian,                                         |
        +------+-------------------------------------------------+
        | N    | converts Lua variable to a 4-byte integer, and  |
        |      | stores the integer in the resulting string, big |
        +------+-------------------------------------------------+
        | q, Q | converts Lua variable to an 8-byte integer, and |
        |      | stores the integer in the resulting string, big |
        |      | endian,                                         |
        +------+-------------------------------------------------+
        | f    | converts Lua variable to a 4-byte float, and    |
        |      | stores the float in the resulting string        |
        +------+-------------------------------------------------+
        | d    | converts Lua variable to a 8-byte double, and   |
        |      | stores the double in the resulting string       |
        +------+-------------------------------------------------+
        | a, A | converts Lua variable to a sequence of bytes,   |
        |      | and stores the sequence in the resulting string |
        +------+-------------------------------------------------+

    :param string format: string containing format specifiers
    :param scalar-value argument(s): scalar values to be formatted
    :return: a binary string containing all arguments,
             packed according to the format specifiers.
    :rtype:  string

    Possible errors: unknown format specifier.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> pickle = require('pickle')
        ---
        ...
        tarantool> box.space.tester:insert{0, 'hello world'}
        ---
        - [0, 'hello world']
        ...
        tarantool> box.space.tester:update({0}, {{'=', 2, 'bye world'}})
        ---
        - [0, 'bye world']
        ...
        tarantool> box.space.tester:update({0}, {
                 >   {'=', 2, pickle.pack('iiA', 0, 3, 'hello')}
                 > })
        ---
        - [0, "\0\0\0\0\x03\0\0\0hello"]
        ...
        tarantool> box.space.tester:update({0}, {{'=', 2, 4}})
        ---
        - [0, 4]
        ...
        tarantool> box.space.tester:update({0}, {{'+', 2, 4}})
        ---
        - [0, 8]
        ...
        tarantool> box.space.tester:update({0}, {{'^', 2, 4}})
        ---
        - [0, 12]
        ...

.. function:: unpack(format, binary-string)

    Counterpart to ``pickle.pack()``.
    Warning: if format specifier 'A' is used, it must be the last item.

    :param string format:
    :param string binary-string:

    :return: A list of strings or numbers.
    :rtype:  table

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> pickle = require('pickle')
        ---
        ...
        tarantool> tuple = box.space.tester:replace{0}
        ---
        ...
        tarantool> string.len(tuple[1])
        ---
        - 1
        ...
        tarantool> pickle.unpack('b', tuple[1])
        ---
        - 48
        ...
        tarantool> pickle.unpack('bsi', pickle.pack('bsi', 255, 65535, 4294967295))
        ---
        - 255
        - 65535
        - 4294967295
        ...
        tarantool> pickle.unpack('ls', pickle.pack('ls', tonumber64('18446744073709551615'), 65535))
        ---
        ...
        tarantool> num, num64, str = pickle.unpack('slA', pickle.pack('slA', 666,
                 > tonumber64('666666666666666'), 'string'))
        ---
        ...

.. _pack: http://perldoc.perl.org/functions/pack.html
