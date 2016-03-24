-------------------------------------------------------------------------------
                            Package `tdb`
-------------------------------------------------------------------------------

The Tarantool Debugger (abbreviation = tdb) can be used with any Lua program.
The operational features include: setting breakpoints, examining variables,
going forward one line at a time, backtracing, and showing information about
fibers. The display features include: using different colors for different
situations, including line numbers, and adding hints.

It is not supplied as part of the Tarantool repository; it must be installed
separately. Here is the usual way:

.. code-block:: bash

    git clone --recursive https://github.com/Sulverus/tdb
    cd tdb
    make
    sudo make install prefix=/usr/share/tarantool/

To initiate tdb within a Lua program and set a breakpoint, edit the program to
include these lines:

.. code-block:: lua

    tdb = require('tdb')
    tdb.start()

To start the debugging session, execute the Lua program. Execution will stop at
the breakpoint, and it will be possible to enter debugging commands.

=================================================
               Debugger Commands
=================================================

:codebold:`bt`
    Backtrace -- show the stack (in red), with program/function names and line
    numbers of whatever has been invoked to reach the current line.

:codebold:`c`
    Continue till next breakpoint or till program ends.

:codebold:`e`
    Enter evaluation mode. When the program is in evaluation mode, one can
    execute certain Lua statements that would be valid in the context. This is
    particularly useful for displaying the values of the program's variables.
    Other debugger commands will not work until one exits evaluation mode by
    typing :codebold:`-e`.

:codebold:`-e`
    Exit evaluation mode.

:codebold:`f`
    Display the fiber id, the program name, and the percentage of memory used,
    as a table.

:codebold:`n`
    Go to the next line, skipping over any function calls.

:codebold:`globals`
    Display names of variables or functions which are defined as global.

:codebold:`h`
    Display a list of debugger commands.

:codebold:`locals`
    Display names and values of variables, for example the control variables of
    a Lua "for" statement.

:codebold:`q`
    Quit immediately.

=================================================
              Example Session
=================================================

Put the following program in a default directory and call it "example.lua":

.. code-block:: lua

  tdb = require('tdb')
  tdb.start()
  i = 1
  j = 'a' .. i
  print('end of program')

Now start Tarantool, using example.lua as the initialization file

.. cssclass:: highlight
.. parsed-literal::

    $ :codebold:`tarantool example.lua`

The screen should now look like this:

.. cssclass:: highlight
.. parsed-literal::

    $ :codebold:`tarantool example.lua`
    :codeblue:`(TDB)`  :codegreen:`Tarantool debugger v.0.0.3. Type h for help`
    example.lua
    :codeblue:`(TDB)`  :codegreen:`[example.lua]`
    :codeblue:`(TDB)`  :codenormal:`3: i = 1`
    :codeblue:`(TDB)>`

Debugger prompts are blue, debugger hints and information
are green, and the current line -- line 3 of example.lua --
is the default color. Now enter six debugger commands:

.. code-block:: lua

    n  -- go to next line
    n  -- go to next line
    e  -- enter evaluation mode
    j  -- display j
    -e -- exit evaluation mode
    q  -- quit

The screen should now look like this:

.. cssclass:: highlight
.. parsed-literal::

    $ :codebold:`tarantool example.lua`
    :codeblue:`(TDB)`  :codegreen:`Tarantool debugger v.0.0.3. Type h for help`
    example.lua
    :codeblue:`(TDB)`  :codegreen:`[example.lua]`
    :codeblue:`(TDB)`  :codenormal:`3: i = 1`
    :codeblue:`(TDB)>` n
    :codeblue:`(TDB)`  :codenormal:`4: j = 'a' .. i`
    :codeblue:`(TDB)>` n
    :codeblue:`(TDB)`  :codenormal:`5: print('end of program')`
    :codeblue:`(TDB)>` e
    :codeblue:`(TDB)`  :codegreen:`Eval mode ON`
    :codeblue:`(TDB)>` j
    j       a1
    :codeblue:`(TDB)>` -e
    :codeblue:`(TDB)`  :codegreen:`Eval mode OFF`
    :codeblue:`(TDB)>` q

Another debugger example can be found here_.

.. _here: https://github.com/sulverus/tdb
