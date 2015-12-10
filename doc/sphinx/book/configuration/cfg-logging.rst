    :confval:`log_level`, |br|
    :confval:`logger`, |br|
    :confval:`logger_nonblock`, |br|
    :confval:`too_long_threshold` |br|

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
    ``logger`` is specified, the log is sent to the file named in the string.
    Example setting: |br|
    box.cfg{logger = 'tarantool.log' } |br|
    This will open :file:`tarantool.log` for output on the server’s default
    directory. If ``logger`` string begins with a pipe, for example |br|
    box.cfg{logger = '| cronolog tarantool.log' } |br|
    the program specified in the option (in this case, cronolog) is executed at server start and all
    log messages are sent to the standard input (``stdin``) of cronolog.

    When logging to a file, tarantool reopens the log on SIGHUP. When log is
    a program, its pid is saved in :func:`log.logger_pid` variable. You need
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
