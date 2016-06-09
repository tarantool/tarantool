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
    error stream by default, but this can be changed with the :ref:`logger <log-label>`
    configuration parameter.

    Type: integer |br|
    Default: 5 |br|
    Dynamic: **yes** |br|

.. _log-label:

.. confval:: logger

    By default, the log is sent to the standard error stream (``stderr``). If
    ``logger`` is specified, the log is sent to a file, or to a pipe, or to
    the system logger.

    Example setting:

    .. code-block:: lua

        box.cfg{logger = 'tarantool.log'}
        -- or
        box.cfg{logger = 'file: tarantool.log'}

    This will open the file ``tarantool.log`` for output on the server’s default
    directory. If the ``logger`` string has no prefix or has the prefix "file:",
    then the string is interpreted as a file path.

    Example setting:

    .. code-block:: lua

        box.cfg{logger = '| cronolog tarantool.log'}
        -- or
        box.cfg{logger = 'pipe: cronolog tarantool.log'}'

    This will start the program ``cronolog`` when the server starts, and
    will send all log messages to the standard input (``stdin``) of cronolog.
    If the ``logger`` string begins with '|' or has the prefix "pipe:",
    then the string is interpreted as a Unix `pipeline`_.

    Example setting:

    .. code-block:: lua

        box.cfg{logger = 'syslog:identity=tarantool'}
        -- or
        box.cfg{logger = 'syslog:facility=user'}
        -- or
        box.cfg{logger = 'syslog:identity=tarantool,facility=user'}

    If the ``logger`` string has the prefix "syslog:", then the string is
    interpreted as a message for the `syslogd`_ program which normally is
    running in the background of any Unix-like platform. One can optionally
    specify an ``identity``, a ``facility``, or both. The ``identity`` is an
    arbitrary string, default value = ``tarantool``, which will be placed at
    the beginning of all messages. The facility is an abbreviation for the
    name of one of the `syslog`_ facilities, default value = ``user``, which
    tell syslogd where the message should go.

    Possible values for ``facility`` are: auth, authpriv, cron, daemon, ftp,
    kern, lpr, mail, news, security, syslog, user, uucp, local0, local1, local2,
    local3, local4, local5, local6, local7.

    The ``facility`` setting is currently ignored but will be used in the future.

    When logging to a file, tarantool reopens the log on SIGHUP. When log is
    a program, its pid is saved in the :func:`log.logger_pid` variable. You need
    to send it a signal to rotate logs.

    Type: string |br|
    Default: null |br|
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

.. _logging_example:

**Logging Example:**

This will illustrate how "rotation" works, that is, what happens when
the server is writing to a log and signals are used when archiving it.

Start with two terminal shells, Terminal #1 and Terminal#2.

On Terminal#1: start an interactive Tarantool session, then say the logging will
go to "Log_file", then put a message "Log Line #1" in the log file:

.. code-block:: lua

    box.cfg{logger='Log_file'}
    log = require('log')
    log.info('Log Line #1')

On Terminal#2: use :codenormal:`mv` so the log file is now named "Log_file.bak".
The result of this is: the next log message will go to Log_file.bak. |br|

.. cssclass:: highlight
.. parsed-literal::

    mv Log_file Log_file.bak

On Terminal#1: put a message "Log Line #2" in the log file. |br|

.. code-block:: lua

    log.info('Log Line #2')

On Terminal#2: use :codenormal:`ps` to find the process ID of the Tarantool server. |br|

.. cssclass:: highlight
.. parsed-literal::

    ps -A | grep tarantool

On Terminal#2: use 'kill -HUP' to send a SIGHUP signal to the Tarantool server.
The result of this is: Tarantool will open Log_file again, and
the next log message will go to Log_file.
(The same effect could be accomplished by executing log.rotate() on the server.) |br|

.. cssclass:: highlight
.. parsed-literal::

    kill -HUP *process_id*

On Terminal#1: put a message "Log Line #3" in the log file.

.. code-block:: lua

    log.info('Log Line #3')

On Terminal#2: use 'less' to examine files. Log_file.bak will have these lines,
except that the date and time will depend on when the example is done:

.. cssclass:: highlight
.. parsed-literal::

    2015-11-30 15:13:06.373 [27469] main/101/interactive I> Log Line #1`
    2015-11-30 15:14:25.973 [27469] main/101/interactive I> Log Line #2`

and Log_file will have

.. cssclass:: highlight
.. parsed-literal::

    log file has been reopened
    2015-11-30 15:15:32.629 [27469] main/101/interactive I> Log Line #3

.. _pipeline: https://en.wikipedia.org/wiki/Pipeline_%28Unix%29
.. _syslogd: https://en.wikipedia.org/wiki/Syslog
.. _syslog: http://www.rfc-base.org/txt/rfc-5424.txt
