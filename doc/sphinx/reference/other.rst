-------------------------------------------------------------------------------
                               Miscellaneous
-------------------------------------------------------------------------------

.. function:: tonumber64(value)

    Convert a string or a Lua number to a 64-bit integer. The result can be
    used in arithmetic, and the arithmetic will be 64-bit integer arithmetic
    rather than floating-point arithmetic. (Operations on an unconverted Lua
    number use floating-point arithmetic.) The ``tonumber64()`` function is
    added by Tarantool; the name is global.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> type(123456789012345), type(tonumber64(123456789012345))
        ---
        - number
        - number
        ...
        tarantool> i = tonumber64('1000000000')
        ---
        ...
        tarantool> type(i), i / 2, i - 2, i * 2, i + 2, i % 2, i ^ 2
        ---
        - number
        - 500000000
        - 999999998
        - 2000000000
        - 1000000002
        - 0
        - 1000000000000000000
        ...

.. function:: dostring(lua-chunk-string [, lua-chunk-string-argument ...])

    Parse and execute an arbitrary chunk of Lua code. This function is mainly
    useful to define and run Lua code without having to introduce changes to
    the global Lua environment.

    :param string lua-chunk-string: Lua code
    :param lua-value lua-chunk-string-argument: zero or more scalar values
                            which will be appended to, or substitute for,
                            items in the Lua chunk.
    :return: whatever is returned by the Lua code chunk.

    Possible errors: If there is a compilation error, it is raised as a Lua error.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> dostring('abc')
        ---
        error: '[string "abc"]:1: ''='' expected near ''<eof>'''
        ...
        tarantool> dostring('return 1')
        ---
        - 1
        ...
        tarantool> dostring('return ...', 'hello', 'world')
        ---
        - hello
        - world
        ...
        tarantool> dostring([[
                 >   local f = function(key)
                 >     local t = box.space.tester:select{key}
                 >     if t ~= nil then
                 >       return t[1]
                 >     else
                 >       return nil
                 >     end
                 >   end
                 >   return f(...)]], 1)
        ---
        - null
        ...

.. _double square brackets: http://www.lua.org/pil/2.4.html
