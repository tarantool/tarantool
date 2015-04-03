.. confval:: log_level

    How verbose the logging is. There are six log verbosity classes:

    * 1 – ``SYSERROR``
    * 2 – ``ERROR``
    * 3 – ``CRITICAL``
    * 4 – ``WARNING``
    * 5 – ``INFO``
    * 6 – ``DEBUG``

    By setting log_level, one can enable logging of all classes below
    or equal to the given level. Tarantool prints its logs to the standard
    error stream by default, but this can be changed with the :confval:`logger`
    configuration parameter.

    Type: integer |br|
    Default: 5 |br|
    Dynamic: **yes** |br|

.. confval:: logger

    By default, the log is sent to the standard error stream (``stderr``). If
    ``logger`` is specified, the log is sent to the file named in the string.
    Example setting:

    .. code-block:: lua

        box.cfg{
            logger = 'tarantool.log'
        }

    This will open :file:`tarantool.log` for output on the server’s default
    directory. If ``logger`` string begins with a pipe, for example

    .. code-block:: lua

        box.cfg{
            logger = '| cronolog tarantool.log'
        }

    the program specified in the option is executed at server start and all
    log messages are send to the standart input.

    When logging to a file, tarantool reopens the log on SIGHUP. When log is
    a program, it’s pid is saved in :func:`log.logger_pid` variable. You need
    to send it a signal to rotate logs.

    Type: string |br|
    Default: "null" |br|
    Dynamic: no |br|

.. confval:: logger_nonblock

    If ``logger_nonblock`` equals true, Tarantool does not block on the log
    file descriptor when it’s not ready for write, and drops the message
    instead. If :confval:`log_level` is high, and a lot of messages go to the
    log file, setting ``logger_nonblock`` to true may improve logging
    performance at the cost of some log messages getting lost.

    Type: boolean |br|
    Default: true |br|
    Dynamic: no |br|

.. confval:: too_long_threshold

    If processing a request takes longer than the given value (in seconds),
    warn about it in the log. Has effect only if :confval:`log_level` is
    more than or equal to 4 (WARNING).

    Type: float |br|
    Default: 0.5 |br|
    Dynamic: **yes** |br|
