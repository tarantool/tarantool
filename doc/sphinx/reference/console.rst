-------------------------------------------------------------------------------
                                   Package `console`
-------------------------------------------------------------------------------

The console package allows one Tarantool server to access another Tarantool
server, and allows one Tarantool server to start listening on an administrative
host/port.

.. module:: console

.. function:: connect(uri [, options])

    Connect to the server at :ref:`URI`, change the prompt from 'tarantool' to
    'host:port', and act henceforth as a client until the user ends the
    session or types ``control-D``.

    The console.connect function allows one Tarantool server, in interactive
    mode, to access another Tarantool server over a TCP connection. Subsequent
    requests will appear to be handled locally, but in reality the requests
    are being sent to the remote server and the local server is acting as a
    client. Once connection is successful, the prompt will change and
    subsequent requests are sent to, and executed on, the remote server.
    Results are displayed on the local server. To return to local mode,
    enter ``control-D``.

    There are no restrictions on the types of requests that can be entered,
    except those which are due to privilege restrictions -- by default the
    login to the remote server is done with user name = 'guest'. The remote
    server could allow for this by granting at least one privilege:
    ``box.schema.user.grant('guest','execute','universe')``.

    :param string uri:
    :param table options: The options may be necessary if the Tarantool
                          server at host:port requires authentication.
                          In such a case the connection might look
                          something like:
                          ``console.connect('netbox:123@127.0.0.1'})``

    :return: nil
    :except: the connection will fail if the target Tarantool server
             was not initiated with ``box.cfg{listen=...}``.

    .. code-block:: lua

        tarantool> console = require('console')
        ---
        ...
        tarantool> console.connect('198.18.44.44:3301')
        ---
        ...
        198.18.44.44:3301> -- prompt is telling us that server is remote

.. function:: listen(URI)

    Listen on URI. The primary way of listening for incoming requests
    is via the connection-information string, or :ref:`URI`, specified in ``box.cfg{listen=...}``.
    The alternative way of listening is via the URI
    specified in ``console.listen(...)``. This alternative way is called
    "administrative" or simply "admin port".
    The listening is usually over a local host with a Unix socket,
    specified with host = 'unix/', port = 'path/to/something.sock'.

    :param string uri:

    The "admin" address is the :ref:`URI` to listen on for administrative
    connections. It has no default value, so it must be specified if
    connections will occur via telnet. It is not used unless assigned a
    value. The parameters are expressed with :ref:`URI` = Universal Resource
    Identifier format, for example "unix://unix_domain_socket", or as a
    numeric TCP port. Connections are often made with telnet.
    A typical port value is 3313.
