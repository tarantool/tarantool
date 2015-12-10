.. _package-console:

-------------------------------------------------------------------------------
                                   Package `console`
-------------------------------------------------------------------------------

The console package allows one Tarantool server to access another Tarantool
server, and allows one Tarantool server to start listening on an administrative
host/port.

.. module:: console

.. function:: connect(uri)

    Connect to the server at :ref:`URI`, change the prompt from ':samp:`tarantool>`' to
    ':samp:`{uri}>`', and act henceforth as a client until the user ends the
    session or types :code:`control-D`.

    The console.connect function allows one Tarantool server, in interactive
    mode, to access another Tarantool server. Subsequent
    requests will appear to be handled locally, but in reality the requests
    are being sent to the remote server and the local server is acting as a
    client. Once connection is successful, the prompt will change and
    subsequent requests are sent to, and executed on, the remote server.
    Results are displayed on the local server. To return to local mode,
    enter :code:`control-D`.

    If the Tarantool server at :samp:`uri` requires authentication, the
    connection might look something like:
    :code:`console.connect('admin:secretpassword@distanthost.com:3301')`.

    There are no restrictions on the types of requests that can be entered,
    except those which are due to privilege restrictions -- by default the
    login to the remote server is done with user name = 'guest'. The remote
    server could allow for this by granting at least one privilege:
    :code:`box.schema.user.grant('guest','execute','universe')`.

    :param string uri: the URI of the remote server

    :return: nil

    Possible errors: the connection will fail if the target Tarantool server
    was not initiated with :code:`box.cfg{listen=...}`.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> console = require('console')
        ---
        ...
        tarantool> console.connect('198.18.44.44:3301')
        ---
        ...
        198.18.44.44:3301> -- prompt is telling us that server is remote

.. function:: listen(uri)

    Listen on :ref:`URI`. The primary way of listening for incoming requests
    is via the connection-information string, or URI, specified in :code:`box.cfg{listen=...}`.
    The alternative way of listening is via the URI
    specified in :code:`console.listen(...)`. This alternative way is called
    "administrative" or simply "admin port".
    The listening is usually over a local host with a Unix socket,
    specified with host = 'unix/', port = 'path/to/something.sock'.

    :param string uri: the URI of the local server

    The "admin" address is the URI to listen on for administrative
    connections. It has no default value, so it must be specified if
    connections will occur via telnet.
    The parameter is expressed with URI = Universal Resource
    Identifier format, for example "/tmpdir/unix_domain_socket.sock", or as a
    numeric TCP port. Connections are often made with telnet.
    A typical port value is 3313.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> console = require('console')
        ---
        ...
        tarantool> console.listen('unix/:/tmp/X.sock')
        ... main/103/console/unix/:/tmp/X I> started
        ---
        - fd: 6
          name:
            host: unix/
            family: AF_UNIX
            type: SOCK_STREAM
            protocol: 0
            port: /tmp/X.sock
        ...

.. function:: start()

    Start the console on the current interactive terminal.

    **Example:**

    A special use of ``console.start()`` is with
    :ref:`initialization files <init-label>`.
    Normally, if one starts the tarantool server with
    :samp:`tarantool {initialization file}`
    there is no console. This can be remedied by adding
    these lines at the end of the initialization file:

    .. code-block:: lua

        console = require('console')
        console.start()
