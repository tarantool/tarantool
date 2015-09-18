=====================================================================
                            Python
=====================================================================

Here is a complete Python program that inserts :code:`[99999,'Value','Value']` into
space :code:`examples` via the high-level Python API.

.. code-block:: python

    #!/usr/bin/python
    from tarantool import Connection

    c = Connection("127.0.0.1", 3301)
    result = c.insert("examples",(99999,'Value', 'Value'))
    print result

To prepare, paste the code into a file named example.py and install
tarantool-python with either :code:`pip install tarantool\>0.4` to install
in :code:`/usr` (requires **root** privilege) or :code:`pip install tarantool\>0.4 --user`
to install in :code:`~` i.e. user's default directory. Before trying to run,
check that the server is listening and that examples exists, as :ref:`described earlier <connector-setting>`.
To run the program, say :code:`python example.py`. The program will connect
to the server, will send the request, and will not throw an exception if
all went well. If the tuple already exists, the program will throw
:code:`DatabaseException(“Duplicate key exists in unique index”)`.

The example program only shows one request and does not show all that's
necessary for good practice. For that, see http://github.com/tarantool/tarantool-python.
For an example of a Python API for `Queue managers on Tarantool`_, see
https://github.com/tarantool/tarantool-queue-python.

.. _Queue managers on Tarantool: https://github.com/tarantool/queue
