-------------------------------------------------------------------------------
                            Package `clock`
-------------------------------------------------------------------------------

The ``clock`` package returns time values derived from
the Posix / C CLOCK_GETTIME_ function or equivalent.
Most functions in the package return a number of seconds;
functions whose names end in "64" return a 64-bit number of nanoseconds.

.. module:: clock

.. function:: time()
              time64()
              realtime()
              realtime64()

    The wall clock time. Derived from C function clock_gettime(CLOCK_REALTIME).
    Approximately the same as os.clock().
    This is the best function for knowing what the official time is,
    as determined by the system administrator. |br|
    See also :func:`fiber.time64 <fiber.time64>`.

    :return: seconds or nanoseconds since epoch (1970-01-01 00:00:00), adjusted.
    :rtype: number or number64

    **Example:**

    .. code-block:: lua

        -- This will print an approximate number of years since 1970.
        clock = require('clock')
        print(clock.time() / (365*24*60*60))

.. function:: monotonic()
              monotonic64()

    The monotonic time. Derived from C function clock_gettime(CLOCK_MONOTONIC).
    Monotonic time is similar to wall clock time but is not affected by changes
    to or from daylight saving time, or by changes done by a user.
    This is the best function to use with benchmarks that need to calculate elapsed time.

    :return: seconds or nanoseconds since the last time that the computer was booted.
    :rtype: number or number64

    **Example:**

    .. code-block:: lua

        -- This will print nanoseconds since the start.
        clock = require('clock')
        print(clock.monotonic64())

.. function:: proc()
              proc64()

    The processor time. Derived from C function clock_gettime(CLOCK_PROCESS_CPUTIME_ID).
    This is the best function to use with benchmarks that need to calculate
    how much time has been spent within a CPU.

    :return: seconds or nanoseconds since processor start.
    :rtype: number or number64

    **Example:**

    .. code-block:: lua

        -- This will print nanoseconds in the CPU since the start.
        clock = require('clock')
        print(clock.proc64())

.. function:: thread()
              thread64()

    The thread time. Derived from C function clock_gettime(CLOCK_THREAD_CPUTIME_ID).
    This is the best function to use with benchmarks that need to calculate
    how much time has been spent within a thread within a CPU.

    :return: seconds or nanoseconds since thread start.
    :rtype: number or number64

    **Example:**

    .. code-block:: lua

        -- This will print seconds in the thread since the start.
        clock = require('clock')
        print(clock.thread64())

.. function:: bench(function [, function parameters ...])

    The time that a function takes within a processor.
    This function uses clock.proc(), therefore it calculates elapsed CPU time.
    Therefore it is not useful for showing actual elapsed time.

    Parameters:

    * :samp:`{function}` = function or function reference;
    * :samp:`{function parameters}` = whatever values are required by the function.

    :return: table. first element = seconds of CPU time; second element = whatever the function returns.
    :rtype: table

    **Example:**

    .. code-block:: lua

        -- Benchmark a function which sleeps 10 seconds.
        -- NB: bench() will not calculate sleep time.
        -- So the returned value will be {a number less than 10, 88}.
        clock = require('clock')
        fiber = require('fiber')
        function f(param)
          fiber.sleep(param)
          return 88
        end
        clock.bench(f,10)

.. _CLOCK_GETTIME: http://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_getres.html
