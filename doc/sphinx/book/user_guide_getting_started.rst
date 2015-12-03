-------------------------------------------------------------------------------
                        Getting started
-------------------------------------------------------------------------------


If the installation has already been done, then you should try it out. So we've
provided some instructions that you can use to make a temporary “sandbox”. In a
few minutes you can start the server and type in some database-manipulation
statements. The section about sandbox is “`Starting Tarantool and making your first database`_”.

.. _first database:

=====================================================================
        Starting Tarantool and making your first database
=====================================================================

Here is how to create a simple test database after installing.

Create a new directory. It's just for tests, you can delete it when the tests are over.

.. code-block:: console

    $ mkdir ~/tarantool_sandbox
    $ cd ~/tarantool_sandbox

Start the server. The server name is tarantool.

.. code-block:: console

    $ # if you downloaded a binary with apt-get or yum, say this:
    $ /usr/bin/tarantool
    $ # if you downloaded and untarred a binary
    $ # tarball to ~/tarantool, say this:
    $ ~/tarantool/bin/tarantool
    $ # if you built from a source download, say this:
    $ ~/tarantool/src/tarantool

The server starts in interactive mode and outputs a command prompt.
To turn on the database, :mod:`configure <box.cfg>` it. This minimal example is sufficient:

.. code-block:: tarantoolsession

    tarantool> box.cfg{listen = 3301}

If all goes well, you will see the server displaying progress as it
initializes, something like this:

.. code-block:: tarantoolsession

    tarantool> box.cfg{listen = 3301}
    2014-08-07 09:41:41.077 ... version 1.6.3-439-g7e1011b
    2014-08-07 09:41:41.077 ... log level 5
    2014-08-07 09:41:41.078 ... mapping 1073741824 bytes for a shared arena...
    2014-08-07 09:41:41.079 ... initialized
    2014-08-07 09:41:41.081 ... initializing an empty data directory
    2014-08-07 09:41:41.095 ... creating './00000000000000000000.snap.inprogress'
    2014-08-07 09:41:41.095 ... saving snapshot './00000000000000000000.snap.inprogress'
    2014-08-07 09:41:41.127 ... done
    2014-08-07 09:41:41.128 ... primary: bound to 0.0.0.0:3301
    2014-08-07 09:41:41.128 ... ready to accept requests

Now that the server is up, you could start up a different shell
and connect to its primary port with:

.. code-block:: console

    $ telnet 0 3301

but for example purposes it is simpler to just leave the server
running in "interactive mode". On production machines the
interactive mode is just for administrators, but because it's
convenient for learning it will be used for most examples in
this manual. Tarantool is waiting for the user to type instructions.

To create the first space and the first :ref:`index <box.index>`, try this:

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('tester')
    tarantool> s:create_index('primary', {
             >   type = 'hash',
             >   parts = {1, 'NUM'}
             > })

To insert three “tuples” (our name for “records”) into the first “space” of the database try this:

.. code-block:: tarantoolsession

    tarantool> t = s:insert({1})
    tarantool> t = s:insert({2, 'Music'})
    tarantool> t = s:insert({3, 'Length', 93})

To select a tuple from the first space of the database, using the first defined key, try this:

.. code-block:: tarantoolsession

    tarantool> s:select{3}

Your terminal screen should now look like this:

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('tester')
    2014-06-10 12:04:18.158 ... creating './00000000000000000002.xlog.inprogress'
    ---
    ...
    tarantool>s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
    ---
    ...
    tarantool> t = s:insert{1}
    ---
    ...
    tarantool> t = s:insert{2, 'Music'}
    ---
    ...
    tarantool> t = s:insert{3, 'Length', 93}
    ---
    ...
    tarantool> s:select{3}
    ---
    - - [3, 'Length', 93]
    ...
    tarantool> 

Now, to prepare for the example in the next section, try this:

.. code-block:: tarantoolsession

    tarantool> box.schema.user.grant('guest', 'read,write,execute', 'universe')

.. _tarantool.org/dist/stable: http://tarantool.org/dist/stable
.. _tarantool.org/dist/master: http://tarantool.org/dist/master


=====================================================================
        Starting another Tarantool instance and connecting remotely
=====================================================================

In the previous section the first request was with :code:`box.cfg{listen = 3301}`.
The :code:`listen` value can be any form of URI (uniform resource identifier);
in this case it's just a local port: port 3301.
It's possible to send requests to the listen URI via (a) telnet,
(b) a connector (which will be the subject of Chapter 8),
or (c) another instance of Tarantool. Let's try (c).

Switch to another terminal.
On Linux, for example, this means starting another instance of a Bash shell.
There is no need to use cd to switch to the :code:`~/tarantool_sandbox` directory.

Start the second instance of Tarantool. The server name is tarantool.

.. code-block:: console

    $ # if you downloaded a binary with apt-get or yum, say this:
    $ /usr/bin/tarantool
    $ # if you downloaded and untarred a
    $ # binary tarball to ~/tarantool, say this:
    $ ~/tarantool/bin/tarantool
    $ # if you built from a source download, say this:
    $ ~/tarantool/src/tarantool

Try these requests:

.. code-block:: tarantoolsession

    tarantool> console = require('console')
    tarantool> console.connect('localhost:3301')
    tarantool> box.space.tester:select{2}

The requests are saying "use the :ref:`console package <package-console>`
to connect to the Tarantool server that's listening
on ``localhost:3301``, send a request to that server,
and display the result." The result in this case is
one of the tuples that was inserted earlier.
Your terminal screen should now look like this:

...

.. code-block:: tarantoolsession

    tarantool> console = require('console')
    ---
    ...
    tarantool> console.connect('localhost:3301')
    2014-08-31 12:46:54.650 [32628] main/101/interactive I> connected to localhost:3301
    ---
    ...
    localhost:3301> box.space.tester:select{2}
    ---
    - - [2, 'Music']
    ...
    localhost:3301> 

You can repeat :code:`box.space...:insert{}` and :code:`box.space...:select{}`
indefinitely, on either Tarantool instance.
When the testing is over: To drop the space: :code:`s:drop()`.
To stop tarantool: Ctrl+C. To stop tarantool (an alternative):
:code:`os.exit()`. To stop tarantool (from another terminal):
:code:`sudo pkill -f tarantool`.
To destroy the test: :code:`rm -r ~/tarantool_sandbox`.

To review ... If you followed all the instructions
in this chapter, then so far you have: installed Tarantool
from either a binary or a source repository,
started up the Tarantool server, inserted and selected tuples.
