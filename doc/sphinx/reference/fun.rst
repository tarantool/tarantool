-------------------------------------------------------------------------------
                                Package `fun`
-------------------------------------------------------------------------------

Lua fun, also known as the Lua Functional Library, takes advantage
of the features of LuaJIT to help users create complex functions.
Inside the package are "sequence processors" such as
map, filter, reduce, zip -- they take a user-written function as
an argument and run it against every element in a sequence, which
can be faster or more convenient than a user-written loop.
Inside the package are "generators" such as range, tabulate, and
rands -- they return a bounded or boundless series of values.
Within the package are "reducers", "filters", "composers" ...
or, in short, all the important features found in languages like
Standard ML, Haskell, or Erlang.

The full documentation is `On the luafun section of github`_.
However, the first chapter can be skipped because installation
is already done, it's inside Tarantool. All that is needed is the usual :code:`require` request.
After that, all the operations described in the
Lua fun manual will work, provided they are preceded by the
name returned by the :code:`require` request.
For example:

.. code-block:: tarantoolsession

    tarantool> fun = require('fun')
    ---
    ...
    tarantool> for _k, a in fun_range(3) do
             >   print(a)
             > end
    1
    2
    3
    ---
    ...

.. _On the luafun section of github: http://rtsisyk.github.io/luafun
