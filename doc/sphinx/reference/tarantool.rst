-------------------------------------------------------------------------------
                            Package `tarantool`
-------------------------------------------------------------------------------

.. module:: tarantool

By saying ``require('tarantool')``, one can answer some questions about how the
tarantool server was built, such as "what flags were used", or "what was the
version of the compiler".

.. _tarantool-build:

Additionally one can see the uptime and the server version and the process id.
Those information items can also be accessed with :func:`box.info` but use of
the tarantool package is recommended.

**Example:**

.. code-block:: tarantoolsession

    tarantool> tarantool = require('tarantool')
    ---
    ...
    tarantool> tarantool
    ---
    - build:
        target: Linux-x86_64-RelWithDebInfo
        options: cmake . -DCMAKE_INSTALL_PREFIX=/usr -DENABLE_TRACE=ON -DENABLE_BACKTRACE=ON
        mod_format: so
        flags: ' -fno-common -fno-omit-frame-pointer -fno-stack-protector -fexceptions
          -funwind-tables -fopenmp -msse2 -std=c11 -Wall -Wextra -Wno-sign-compare -Wno-strict-aliasing
          -fno-gnu89-inline'
        compiler: /usr/bin/x86_64-linux-gnu-gcc /usr/bin/x86_64-linux-gnu-g++
      uptime: 'function: 0x408668e0'
      version: 1.6.8-66-g9093daa
      pid: 'function: 0x40866900'
    ...
    tarantool> tarantool.pid()
    ---
    - 30155
    ...
    tarantool> tarantool.uptime()
    ---
    - 108.64641499519
    ...
