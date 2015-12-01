-------------------------------------------------------------------------------
                            Package `strict`
-------------------------------------------------------------------------------

.. module:: strict

The :code:`strict` package has functions for turning "strict mode" on or off.
When strict mode is on, an attempt to use an undeclared global variable will
cause an error. A global variable is considered "undeclared" if it has never
had a value assigned to it. Often this is an indication of a programming error.

By default strict mode is off, unless tarantool was built with the
``-DCMAKE_BUILD_TYPE=Debug`` option -- see the description of build options
in section :ref:`building-from-source`.

**Example:**

.. code-block:: tarantoolsession

    tarantool> strict = require('strict')
    ---
    ...
    tarantool> strict.on()
    ---
    ...
    tarantool> a = b -- strict mode is on so this will cause an error
    ---
    - error: ... variable ''b'' is not declared'
    ...
    tarantool> strict.off()
    ---
    ...
    tarantool> a = b -- strict mode is off so this will not cause an error
    ---
    ...
