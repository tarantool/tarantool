-------------------------------------------------------------------------------
                            Package `tarantool`
-------------------------------------------------------------------------------

.. module:: tarantool

By saying :code:`require('tarantool')`, one can answer
some questions about how the tarantool server was built,
such as "what flags were used", or "what was the version
of the compiler".

.. _tarantool-build:

Additionally one can see the uptime
and the server version and the process id. Those
information items can also be accessed with
:func:`box.info` but use of the tarantool package is
recommended.

    | EXAMPLE
    | :codenormal:`tarantool>` :codebold:`tarantool = require('tarantool')`
    | :codenormal:`---`
    | :codenormal:`...`
    | |nbsp|
    | :codenormal:`tarantool>` :codebold:`tarantool`
    | :codenormal:`---`
    | :codenormal:`- build:`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`flags: ' -fno-omit-frame-pointer -fno-stack-protector`
    | |nbsp| |nbsp| |nbsp| |nbsp|  |nbsp| |nbsp| :codenormal:`-fopenmp -msse2 -std=c11 -Wall`
    | |nbsp| |nbsp| |nbsp| |nbsp|  |nbsp| |nbsp| :codenormal:`-fno-gnu89-inline'`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`target: Linux-x86_64-Release`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`compiler: /usr/bin/cc /usr/bin/c++`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`options: cmake . -DCMAKE_INSTALL_PREFIX=/usr/local`
    | |nbsp| |nbsp| :codenormal:`uptime: 'function: 0x4109a670'`
    | |nbsp| |nbsp| :codenormal:`version: 1.6.0-3142-g63af2df`
    | |nbsp| |nbsp| :codenormal:`pid: 'function: 0x4109a690'`
    | :codenormal:`...`



