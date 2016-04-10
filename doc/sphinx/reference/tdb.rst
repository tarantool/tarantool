-------------------------------------------------------------------------------
                            Package `tdb`
-------------------------------------------------------------------------------

The Tarantool Debugger (abbreviation = tdb) can be used
with any Lua program. The operational features include:
setting breakpoints, examining variables, going forward
one line at a time, backtracing, and showing information
about fibers. The display features include: using different
colors for different situations, including line numbers,
and adding hints.

It is not supplied as part of the Tarantool repository;
it must be installed separately. Here is the usual way: |br|
:codebold:`git clone --recursive https://github.com/Sulverus/tdb` |br|
:codebold:`cd tdb` |br|
:codebold:`make` |br|
:codebold:`sudo make install prefix=/usr/share/tarantool/`

To initiate tdb within a Lua program and set a breakpoint,
edit the program to include these lines: |br|
:codenormal:`tdb = require('tdb')` |br|
:codenormal:`tdb.start()`

To start the debugging session, execute the Lua program.
Execution will stop at the breakpoint, and it will be
possible to enter debugging commands.

=================================================
               Debugger Commands
=================================================

:codebold:`bt` -- Backtrace -- show the stack
(in red), with program/function names and line numbers
of whatever has been invoked to reach the current line.

:codebold:`c` -- Continue till next breakpoint
or till program ends.

:codebold:`e` -- Enter evaluation mode. When
the program is in evaluation mode, one can execute
certain Lua statements that would be valid in the context.
This is particularly useful for displaying
the values of the program's variables.
Other debugger commands will not work until one
exits evaluation mode by typing :codebold:`-e`.

:codebold:`-e` -- Exit evaluation mode.

:codebold:`n` -- Go to the next line, skipping over
any function calls.

:codebold:`globals` -- Display names of variables
or functions which are defined as global.

:codebold:`h` -- Display a list of debugger commands.

:codebold:`locals` -- Display names and values of
variables, for example the control variables of a
Lua "for" statement.

:codebold:`q` -- Quit immediately.

=================================================
              Example Session
=================================================

Put the following program in a default directory and call it
"example.lua":

  :codenormal:`tdb = require('tdb')` |br|
  :codenormal:`tdb.start()` |br|
  :codenormal:`i = 1` |br|
  :codenormal:`j = 'a' .. i` |br|
  :codenormal:`print('end of program')`

Now start Tarantool, using example.lua as the
initialization file: |br|
:codebold:`tarantool example.lua`

The screen should now look like this: |br|
:codenormal:`$` :codebold:`tarantool example.lua` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`Tarantool debugger v.0.0.3. Type h for help` |br|
:codenormal:`example.lua` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`[example.lua]` |br|
:codeblue:`(TDB)` |nbsp| :codenormal:`3: i = 1` |br|
:codeblue:`(TDB)>` |br|
Debugger prompts are blue, debugger hints and information
are green, and the current line -- line 3 of example.lua --
is the default color. Now enter six debugger commands: |br|
:codebold:`n` |br|
:codebold:`n` |br|
:codebold:`e` |br|
:codebold:`j` |br|
:codebold:`-e` |br|
:codebold:`q` |br|
... These commands mean "go to next line", "go to next line",
"enter evaluation mode", "display j", "exit evaluation mode",
"quit". The screen should now look like this: |br|

:codenormal:`$` :codebold:`tarantool example.lua` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`Tarantool debugger v.0.0.3. Type h for help` |br|
:codenormal:`example.lua` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`[example.lua]` |br|
:codeblue:`(TDB)` |nbsp| :codenormal:`3: i = 1` |br|
:codeblue:`(TDB)>`:codenormal:`n` |br|
:codeblue:`(TDB)` |nbsp| :codenormal:`4: j = 'a' .. i` |br|
:codeblue:`(TDB)>`:codenormal:`n` |br|
:codeblue:`(TDB)` |nbsp| :codenormal:`5: print('end of program')` |br|
:codeblue:`(TDB)>`:codenormal:`e` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`Eval mode ON` |br|
:codeblue:`(TDB)>`:codenormal:`j` |br|
:codenormal:`j` |nbsp| |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`a1` |br|
:codeblue:`(TDB)>`:codenormal:`-e` |br|
:codeblue:`(TDB)` |nbsp| :codegreen:`Eval mode OFF` |br|
:codeblue:`(TDB)>`:codenormal:`q` |br|

Another debugger example can be found here_.

.. _here: https://github.com/sulverus/tdb
