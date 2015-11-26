-------------------------------------------------------------------------------
                            Package `jit`
-------------------------------------------------------------------------------

.. module:: jit

The ``jit`` package has functions for tracing the LuaJIT Just-In-Time compiler's
progress, showing the byte-code or assembler output that the compiler produces,
and in general providing information about what LuaJIT does with Lua code.

.. function:: jit.bc.dump(function)

    Prints the byte code of a function.

    **Example:**

    .. code-block:: lua

        function f()
          print("D")
        end
        jit.bc.dump(f)

    For a list of available options, read `the source code of bc.lua`_.

.. function:: jit.dis_x86.disass(string)

    Prints the i386 assembler code of a string of bytes

    **Example:**

    .. code-block:: lua

        -- Disassemble hexadecimal 97 which is the x86 code for xchg eax, edi
        jit.dis_x86.disass('\x97')

    For a list of available options, read `the source code of dis_x86.lua`_.

.. function:: jit.dis_x64.disass(string)

    Prints the x86-64 assembler code of a string of bytes

    **Example:**

    .. code-block:: lua

        -- Disassemble hexadecimal 97 which is the x86-64 code for xchg eax, edi
        jit.dis_x64.disass('\x97')

    For a list of available options, read `the source code of dis_x64.lua`_.

.. function:: jit.dump.on(option [, output file])
              jit.dump.off()

    Prints the intermediate or machine code of following Lua code

    **Example:**

    .. code-block:: lua

        -- Show the machine code of a Lua "for" loop
        jit.dump.on('m')
        local x = 0;
        for i = 1, 1e6 do
          x = x + i
        end
        print(x)
        jit.dump.off()

    For a list of available options, read `the source code of dump.lua`_.


.. function:: jit.v.on(option [, output file])
              jit.v.off()

    Prints a trace of LuaJIT's progress compiling and interpreting code

    **Example:**

    .. code-block:: lua

        -- Show what LuaJIT is doing for a Lua "for" loop
        jit.v.on()
        local x = 0
        for i = 1, 1e6 do
            x = x + i
        end
        print(x)
        jit.v.off()

    For a list of available options, read `the source code of v.lua`_.


.. _the source code of bc.lua: https://github.com/tarantool/luajit/tree/master/src/jit/bc.lua
.. _the source code of dis_x86.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dis_x86.lua
.. _the source code of dis_x64.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dis_x64.lua
.. _the source code of dump.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dump.lua
.. _the source code of v.lua: https://github.com/tarantool/luajit/tree/master/src/jit/v.lua
