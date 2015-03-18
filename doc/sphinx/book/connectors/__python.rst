=====================================================================
                            Python
=====================================================================

Here is a complete Python program that inserts ``[99999,'Value','Value']`` into
space ``examples`` via the high-level Python API.

.. code-block:: python

    #!/usr/bin/python
    from tarantool import Connection

    c = Connection("127.0.0.1", 3301)
    result = c.insert("examples",(99999,'Value', 'Value'))
    print result

To prepare, paste the code into a file named example.py and install
tarantool-python with either ``pip install tarantool\>0.4`` to install
in ``/usr`` (requires **root** privilege) or ``pip install tarantool\>0.4 --user``
to install in ``~`` i.e. user's default directory. Before trying to run,
check that the server is listening and that examples exists, as `described earlier`_.
To run the program, say ``python example.py``. The program will connect
to the server, will send the request, and will not throw an exception if
all went well. If the tuple already exists, the program will throw
``DatabaseException(“Duplicate key exists in unique index”)``.

The example program only shows one request and does not show all that's
necessary for good practice. For that, see http://github.com/tarantool/tarantool-python.
For an example of a Python API for `Queue managers on Tarantool`_, see
https://github.com/tarantool/tarantool-queue-python.

.. _described earlier: https://en.wikipedia.org/wiki/Cpan
.. _Queue managers on Tarantool: https://github.com/tarantool/queue
