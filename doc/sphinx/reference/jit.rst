-------------------------------------------------------------------------------
                            Package `jit`
-------------------------------------------------------------------------------

.. module:: jit

The :code:`jit` package has functions for
tracing the LuaJIT Just-In-Time compiler's progress, showing the byte-code
or assembler output that the compiler produces, and
in general providing information about what LuaJIT
does with Lua code.

:codenormal:`jit.bc.dump(` :codeitalic:`function` :codenormal:`)` |br|
Prints the byte code of a function. |br|
EXAMPLE |br|
:codebold:`# Show the byte code of a Lua function` |br|
:codebold:`function f() print("D") end` |br|
:codebold:`jit.bc.dump(f)` |br|
For a list of available options, read `the source code of bc.lua`_.

:codenormal:`jit.dis_x86.disass(` :codeitalic:`string` :codenormal:`)` |br|
Prints the i386 assembler code of a string of bytes |br|
EXAMPLE |br|
:codebold:`# Disassemble hexadecimal 97 which is the x86 code for xchg eax, edi` |br|
:codebold:`jit.dis_x86.disass ('\x97')` |br|
For a list of available options, read `the source code of dis_x86.lua`_.

:codenormal:`jit.dis_x64.disass(` :codeitalic:`string` :codenormal:`)` |br|
Prints the x86-64 assembler code of a string of bytes |br|
EXAMPLE |br|
:codebold:`# Disassemble hexadecimal 97 which is the x86-64 code for xchg eax, edi` |br|
:codebold:`jit.dis_x64.disass('\x97')` |br|
For a list of available options, read `the source code of dis_x64.lua`_.

:codenormal:`jit.dump.on(` :codeitalic:`option` :codenormal:`[,` :codeitalic:`output file` :codenormal:`]) / jit.dump.off()` |br|
Prints the intermediate or machine code of following Lua code |br|
EXAMPLE |br|
:codebold:`# Show the machine code of a Lua "for" loop` |br|
:codebold:`jit.dump.on('m')` |br|
:codebold:`local x=0; for i=1,1e6 do x=x+i end; print(x)` |br|
:codebold:`jit.dump.off()` |br|
For a list of available options, read `the source code of dump.lua`_.

:codenormal:`jit.v.on(` :codeitalic:`option` :codenormal:`[,` :codeitalic:`output file` :codenormal:`]) / jit.v.off()` |br|
Prints a trace of LuaJIT's progress compiling and interpreting code |br|
EXAMPLE |br|
:codebold:`# Show what LuaJIT is doing for a Lua "for" loop` |br|
:codebold:`jit.v.on()` |br|
:codebold:`local x=0; for i=1,1e6 do x=x+i end; print(x)` |br|
:codebold:`jit.v.off()` |br|
For a list of available options, read `the source code of v.lua`_.


.. _the source code of bc.lua: https://github.com/tarantool/luajit/tree/master/src/jit/bc.lua
.. _the source code of dis_x86.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dis_x86.lua
.. _the source code of dis_x64.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dis_x64.lua
.. _the source code of dump.lua: https://github.com/tarantool/luajit/tree/master/src/jit/dump.lua
.. _the source code of v.lua: https://github.com/tarantool/luajit/tree/master/src/jit/v.lua

