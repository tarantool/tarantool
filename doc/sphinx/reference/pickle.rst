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

    :except: unknown format specifier.

    | EXAMPLE
    |
    | 
    | :codenormal:`tarantool>` :codebold:`pickle = require('pickle')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:insert{0, 'hello world'}`
    | :codenormal:`---`
    | :codenormal:`- [0, 'hello world']`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:update({0}, {{'=', 2, 'bye world'}})`
    | :codenormal:`---`
    | :codenormal:`- [0, 'bye world']`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:update({0}, {{'=', 2, pickle.pack('iiA', 0, 3, 'hello')}})`
    | :codenormal:`---`
    | :codenormal:`- [0, "\0\0\0\0\x03\0\0\0hello"]`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:update({0}, {{'=', 2, 4}})`
    | :codenormal:`---`
    | :codenormal:`- [0, 4]`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:update({0}, {{'+', 2, 4}})`
    | :codenormal:`---`
    | :codenormal:`- [0, 8]`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.space.tester:update({0}, {{'^', 2, 4}})`
    | :codenormal:`---`
    | :codenormal:`- [0, 12]`
    | :codenormal:`...`

.. function:: unpack(format, binary-string)

    Counterpart to ``pickle.pack()``.

    :param string format:
    :param string binary-string:

    :return: A list of strings or numbers.
    :rtype:  table

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`pickle = require('pickle')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`-- this means following commands must end with '!'`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`tuple = box.space.tester:replace{0}!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`string.len(tuple[1])!`
    | :codenormal:`---`
    | :codenormal:`- 1`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`pickle.unpack('b', tuple[1])!`
    | :codenormal:`---`
    | :codenormal:`- 48`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`pickle.unpack('bsi', pickle.pack('bsi', 255, 65535, 4294967295))!`
    | :codenormal:`---`
    | :codenormal:`- 255`
    | :codenormal:`- 65535`
    | :codenormal:`- 4294967295`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`pickle.unpack('ls', pickle.pack('ls', tonumber64('18446744073709551615'), 65535))!`
    | :codenormal:`---`
    | :codenormal:`- 18446744073709551615`
    | :codenormal:`- 65535`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`num, str, num64 = pickle.unpack('sAl', pickle.pack('sAl', 666, 'string',`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` :codebold:`tonumber64('666666666666666')))!`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console.delimiter('') -- back to normal: commands end with line feed!`

.. _pack: http://perldoc.perl.org/functions/pack.html
