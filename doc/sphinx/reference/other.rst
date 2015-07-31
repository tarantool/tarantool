-------------------------------------------------------------------------------
                               Miscellaneous
-------------------------------------------------------------------------------

.. function:: tonumber64(value)

    Convert a string or a Lua number to a 64-bit integer. The result can be
    used in arithmetic, and the arithmetic will be 64-bit integer arithmetic
    rather than floating-point arithmetic. (Operations on an unconverted Lua
    number use floating-point arithmetic.) The ``tonumber64()`` function is
    added by Tarantool; the name is global.

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`type(123456789012345), type(tonumber64(123456789012345))`
    | :codenormal:`---`
    | :codenormal:`- number`
    | :codenormal:`- number`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`i = tonumber64('1000000000')`
    | :codenormal:`---`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`type(i), i / 2, i - 2, i * 2, i + 2, i % 2, i ^ 2`
    | :codenormal:`---`
    | :codenormal:`- number`
    | :codenormal:`- 500000000`
    | :codenormal:`- 999999998`
    | :codenormal:`- 2000000000`
    | :codenormal:`- 1000000002`
    | :codenormal:`- 0`
    | :codenormal:`- 1000000000000000000`
    | :codenormal:`...`

.. function:: dostring(lua-chunk-string [, lua-chunk-string-argument ...])

    Parse and execute an arbitrary chunk of Lua code. This function is mainly
    useful to define and run Lua code without having to introduce changes to
    the global Lua environment.

    :param string lua-chunk-string: Lua code
    :param lua-value lua-chunk-string-argument: zero or more scalar values
                            which will be appended to, or substitute for,
                            items in the Lua chunk.
    :return: whatever is returned by the Lua code chunk.
    :except: If there is a compilation error, it is raised as a Lua error.

    | EXAMPLE
    |
    | :codenormal:`tarantool>` :codebold:`dostring('abc')`
    | :codenormal:`---`
    | :codenormal:`error: '[string "abc"]:1: ''='' expected near ''<eof>'''`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`dostring('return 1')`
    | :codenormal:`---`
    | :codenormal:`- 1`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`dostring('return ...', 'hello', 'world')`
    | :codenormal:`---`
    | :codenormal:`- hello`
    | :codenormal:`- world`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`console = require('console'); console.delimiter('!')`
    | :codenormal:`tarantool>` :codebold:`-- This means ignore line feeds until next '!'`
    | :codenormal:`tarantool>` :codebold:`-- Use` `double square brackets`_ :codebold:`to enclose multi-line literal here!`
    | :codenormal:`tarantool>` :codebold:`dostring([[local f = function(key)`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` :codebold:`t = box.space.tester:select{key};`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` :codebold:`if t ~= nil then return t[1] else return nil end`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` :codebold:`end`
    | |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`->` :codebold:`return f(...)]], 1)!`
    | :codenormal:`---`
    | :codenormal:`- null`
    | :codenormal:`...`
    | :codenormal:`tarantool> console.delimiter('')!`

.. _double square brackets: http://www.lua.org/pil/2.4.html
