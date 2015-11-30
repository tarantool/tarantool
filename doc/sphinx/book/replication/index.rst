.. _box-replication:

-------------------------------------------------------------------------------
                    Replication
-------------------------------------------------------------------------------

Replication allows multiple Tarantool servers to work on copies of the same
databases. The databases are kept in synch because each server can communicate
its changes to all the other servers. Servers which share the same databases
are a "cluster". Each server in a cluster also has a numeric identifier which
is unique within the cluster, known as the "server id".

To set up replication, it's necessary to set up the master servers which
make the original data-change requests, set up the replica servers which
copy data-change requests from masters, and establish procedures for
recovery from a degraded state.

=====================================================================
                    Replication architecture
=====================================================================

A replica gets all updates from the master by continuously fetching and
applying its write-ahead log (WAL). Each record in the WAL represents a
single Tarantool data-change request such as INSERT or UPDATE or DELETE,
and is assigned a monotonically growing log sequence number (LSN). In
essence, Tarantool replication is row-based: each data change command is
fully deterministic and operates on a single tuple.

A stored program invocation is not written to the write-ahead log. Instead,
log events for actual data-change requests, performed by the Lua code, are
written to the log. This ensures that possible non-determinism of Lua does
not cause replication to go out of sync.

=====================================================================
                       Setting up the master
=====================================================================

To prepare the master for connections from the replica, it's only necessary
to include "listen" in the initial ``box.cfg`` request, for example
``box.cfg{listen=3301}``. A master with enabled "listen" URI can accept
connections from as many replicas as necessary on that URI. Each replica
has its own replication state.

=====================================================================
                        Setting up a replica
=====================================================================

A server requires a valid snapshot (.snap) file. A snapshot file is created
for a server the first time that ``box.cfg`` occurs for it. If this first
``box.cfg`` request occurs without a "replication source" clause, then the
server is a master and starts its own new cluster with a new unique UUID.
If this first ``box.cfg`` request occurs with a "replication source" clause,
then the server is a replica and its snapshot file, along with the cluster
information, is constructed from the write-ahead logs of the master.
Therefore, to start replication, specify :confval:`replication_source`
in a ``box.cfg`` request. When a replica contacts a master for the first time,
it becomes part of a cluster. On subsequent occasions, it should always contact
a master in the same cluster.

Once connected to the master, the replica requests all changes that happened
after the latest local LSN. It is therefore necessary to keep WAL files on
the master host as long as there are replicas that haven't applied them yet.
A replica can be "re-seeded" by deleting all its files (the snapshot .snap
file and the WAL .xlog files), then starting replication again - the replica
will then catch up with the master by retrieving all the master's tuples.
Again, this procedure works only if the master's WAL files are present.

.. NOTE::

    Replication parameters are "dynamic", which allows the replica to become
    a master and vice versa with the help of the :func:`box.cfg` statement.

.. NOTE::

    The replica does not inherit the master's configuration parameters, such
    as the ones that cause the :ref:`snapshot daemon <book-cfg-snapshot_daemon>` to run on the master.
    To get the same behavior, one would have to set the relevant parameters explicitly
    so that they are the same on both master and replica.

=====================================================================
                Recovering from a degraded state
=====================================================================

"Degraded state" is a situation when the master becomes unavailable - due to
hardware or network failure, or due to a programming bug. There is no automatic
way for a replica to detect that the master is gone for good, since sources of
failure and replication environments vary significantly. So the detection of
degraded state requires a human inspection.

However, once a master failure is detected, the recovery is simple: declare
that the replica is now the new master, by saying ``box.cfg{... listen=URI}``.
Then, if there are updates on the old master that were not propagated before
the old master went down, they would have to be re-applied manually.

=============================================================================
        Instructions for quick startup of a new two-server simple cluster
=============================================================================

Step 1. Start the first server thus:

.. cssclass:: highlight
.. parsed-literal::

    box.cfg{listen = *uri#1*}
    -- replace with more restrictive request
    box.schema.user.grant('guest', 'read,write,execute', 'universe')
    box.snapshot()

... Now a new cluster exists.

Step 2. Check where the second server's files will go by looking at its
directories (:confval:`snap_dir` for snapshot files, :confval:`wal_dir` for .xlog files).
They must be empty - when the second server joins for the first time, it
has to be working with a clean slate so that the initial copy of the first
server's databases can happen without conflicts.

Step 3. Start the second server thus:

.. cssclass:: highlight
.. parsed-literal::

    box.cfg{
      listen = *uri#2*,
      replication_source = *uri#1*
    }

... where ``uri#1`` = the :ref:`URI` that the first server is listening on.

That's all.

In this configuration, the first server is the "master" and the second server
is the "replica". Henceforth every change that happens on the master will be
visible on the replica. A simple two-server cluster with the master on one
computer and the replica on a different computer is very common and provides
two benefits: FAILOVER (because if the master goes down then the replica can
take over), or LOAD BALANCING (because clients can connect to either the master
or the replica for select requests).

=====================================================================
                    Monitoring a Replica's Actions
=====================================================================

In :func:`box.info` there is a :code:`box.info.replication.status` field:
"off", "stopped", "connecting", "connected", or "disconnected". |br|
If a replica's status is "connected", then there will be two more fields: |br|
:code:`box.info.replication.idle` = the number of seconds the replica has been idle, |br|
:code:`box.info.replication.lag` = the number of seconds the replica is behind the master.

In the :mod:`log` there is a record of replication activity.
If a primary server is started with:

.. cssclass:: highlight
.. parsed-literal::

    box.cfg{
      <...>,
      logger = *log file name*,
      <...>
    }

then there will be lines in the log file, containing the word "relay",
when a replica connects or disconnects.

=====================================================================
                    Preventing Duplicate Actions
=====================================================================

Suppose that the replica tries to do something that the master has already done.
For example: |br|
:code:`box.schema.space.create('X')` |br|
This would cause an error, "Space X exists".
For this particular situation, the code could be changed to: |br|
:code:`box.schema.space.create('X', {if_not_exists=true})` |br|
But there is a more general solution: the
:samp:`box.once({key}, {function})` method.
If :code:`box.once()` has been called before with the
same :samp:`{key}` value, then :samp:`{function}`
is ignored; otherwise :samp:`{function}` is executed.
Therefore, actions which should only occur once during the
life of a replicated session should be placed in a function
which is executed via :code:`box.once()`. For example:

.. code-block:: lua

    function f()
      box.schema.space.create('X')
    end
    box.once('space_creator', f)

=====================================================================
                    Master-Master Replication
=====================================================================

In the simple master-replica configuration, the master's changes are seen by
the replica, but not vice versa, because the master was specified as the sole
replication source. Starting with Tarantool 1.6, it's possible to go both ways.
Starting with the simple configuration, the first server has to say:

.. cssclass:: highlight
.. parsed-literal::

    box.cfg{ replication_source = *uri#2* }

This request can be performed at any time.

In this configuration, both servers are "masters" and both servers are
"replicas". Henceforth every change that happens on either server will
be visible on the other. The failover benefit is still present, and the
load-balancing benefit is enhanced (because clients can connect to either
server for data-change requests as well as select requests).

If two operations for the same tuple take place "concurrently" (which can
involve a long interval because replication is asynchronous), and one of
the operations is ``delete`` or ``replace``, there is a possibility that
servers will end up with different contents.


=====================================================================
                All the "What If?" Questions
=====================================================================
.. container:: faq

    :Q: What if there are more than two servers with master-master?
    :A: On each server, specify the :confval:`replication_source` for all the
        others. For example, server #3 would have a request:

        .. cssclass:: highlight
        .. parsed-literal::

            box.cfg{
                replication_source = *uri#1*,
                replication_source = *uri#2*
            }

    :Q: What if a server should be taken out of the cluster?
    :A: Run ``box.cfg{}`` again specifying a blank replication source:
        ``box.cfg{replication_source=''}``

    :Q: What if a server leaves the cluster?
    :A: The other servers carry on. If the wayward server rejoins, it will
        receive all the updates that the other servers made while it was away.

    :Q: What if two servers both change the same tuple?
    :A: The last changer wins. For example, suppose that server#1 changes the
        tuple, then server#2 changes the tuple. In that case server#2's change
        overrides whatever server#1 did. In order to keep track of who came last,
        Tarantool implements a `vector clock`_.

    :Q: What if two servers both insert the same tuple?
    :A: If a master tries to insert a tuple which a replica has inserted
        already, this is an example of a severe error. Replication stops.
        It will have to be restarted manually.

    :Q: What if a master disappears and the replica must take over?
    :A: A message will appear on the replica stating that the connection is
        lost. The replica must now become independent, which can be done by
        saying ``box.cfg{replication_source=''}``.

    :Q: What if it's necessary to know what cluster a server is in?
    :A: The identification of the cluster is a UUID which is generated when the
        first master starts for the first time. This UUID is stored in a tuple
        of the :data:`box.space._cluster` system space, and in a tuple of the
        :data:`box.space._schema` system space. So to see it, say:
        ``box.space._schema:select{'cluster'}``

    :Q: What if one of the server's files is corrupted or deleted?
    :A: Stop the server, destroy all the database files (the ones with extension
        "snap" or "xlog" or ".inprogress"), restart the server, and catch up
        with the master by contacting it again (just say
        ``box.cfg{...replication_source=...}``).

    :Q: What if replication causes security concerns?
    :A: Prevent unauthorized replication sources by associating a password with
        every user that has access privileges for the relevant spaces. That way,
        the :ref:`URI` for the :confval:`replication_source` parameter will
        always have to have the long form
        ``replication_source='username:password@host:port'``

.. _vector clock: https://en.wikipedia.org/wiki/Vector_clock

=====================================================================
                    Hands-On Replication Tutorial
=====================================================================

After following the steps here, an administrator will have experience creating
a cluster and adding a replica.

Start two shells. Put them side by side on the screen. (This manual has a tabbed
display showing "Terminal #1". Click the "Terminal #2" tab to switch to the
display of the other shell.)

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-1

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-1-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-1-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-1-content

        .. container:: b-documentation_tab
            :name: terminal-1-1

            .. code-block:: console

                $

        .. container:: b-documentation_tab
            :name: terminal-1-2

            .. code-block:: console

                $

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-1 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-1 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-1-1').height(), $('#terminal-1-2').height());
                    $('#catalog-1-content').height(maxHeight + 10);
                    $('#terminal-1-1').height(maxHeight);
                    $('#terminal-1-2').height(maxHeight);
                    $('#terminal-1-1').show();
                    $('#terminal-1-2').hide();
                });
            })();
        </script>

On the first shell, which we'll call Terminal #1, execute these commands:

.. code-block:: tarantoolsession

    $ # Terminal 1
    $ mkdir -p ~/tarantool_test_node_1
    $ cd ~/tarantool_test_node_1
    $ rm -R ~/tarantool_test_node_1/*
    $ ~/tarantool/src/tarantool
    tarantool> box.cfg{listen = 3301}
    tarantool> box.schema.user.create('replicator', {password = 'password'})
    tarantool> box.schema.user.grant('replicator', 'read,write', 'universe')
    tarantool> box.space._cluster:select({0}, {iterator = 'GE'})

The result is that a new cluster is set up, and the UUID is displayed. Now the
screen looks like this: (except that UUID values are always different):

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-2

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-2-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-2-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-2-content

        .. container:: b-documentation_tab
            :name: terminal-2-1

            .. include:: 1-1.rst

        .. container:: b-documentation_tab
            :name: terminal-2-2

            .. include:: 1-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-2 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-2 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-2-1').height(), $('#terminal-2-2').height());
                    $('#catalog-2-content').height(maxHeight + 10);
                    $('#terminal-2-1').height(maxHeight);
                    $('#terminal-2-2').height(maxHeight);
                    $('#terminal-2-1').show();
                    $('#terminal-2-2').hide();
                });
            })();
        </script>

On the second shell, which we'll call Terminal #2, execute these commands:

.. code-block:: tarantoolsession

    $ # Terminal 2
    $ mkdir -p ~/tarantool_test_node_2
    $ cd ~/tarantool_test_node_2
    $ rm -R ~/tarantool_test_node_2/*
    $ ~/tarantool/src/tarantool
    tarantool> box.cfg{
             >   listen = 3302,
             >   replication_source = 'replicator:password@localhost:3301'
             > }
    tarantool> box.space._cluster:select({0}, {iterator = 'GE'})

The result is that a replica is set up. Messages appear on Terminal #1
confirming that the replica has connected and that the WAL contents have
been shipped to the replica. Messages appear on Terminal #2 showing that
replication is starting. Also on Terminal#2 the _cluster UUID value is
displayed, and it is the same as the _cluster UUID value that was displayed
on Terminal #1, because both servers are in the same cluster.

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-3

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-3-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-3-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-3-content

        .. container:: b-documentation_tab
            :name: terminal-3-1

            .. include:: 2-1.rst

        .. container:: b-documentation_tab
            :name: terminal-3-2

            .. include:: 2-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-3 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-3 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-3-1').height(), $('#terminal-3-2').height());
                    $('#catalog-3-content').height(maxHeight + 10);
                    $('#terminal-3-1').height(maxHeight);
                    $('#terminal-3-2').height(maxHeight);
                    $('#terminal-3-1').show();
                    $('#terminal-3-2').hide();
                });
            })();
        </script>

On Terminal #1, execute these requests:

.. code-block:: tarantoolsession

    tarantool> s = box.schema.space.create('tester')
    tarantool> i = s:create_index('primary', {})
    tarantool> s:insert{1, 'Tuple inserted on Terminal #1'}

Now the screen looks like this:

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-4

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-4-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-4-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-4-content

        .. container:: b-documentation_tab
            :name: terminal-4-1

            .. include:: 3-1.rst

        .. container:: b-documentation_tab
            :name: terminal-4-2

            .. include:: 3-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-4 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-4 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-4-1').height(), $('#terminal-4-2').height());
                    $('#catalog-4-content').height(maxHeight + 10);
                    $('#terminal-4-1').height(maxHeight);
                    $('#terminal-4-2').height(maxHeight);
                    $('#terminal-4-1').show();
                    $('#terminal-4-2').hide();
                });
            })();
        </script>

The creation and insertion were successful on Terminal #1. Nothing has happened
on Terminal #2.

On Terminal #2, execute these requests:

.. code-block:: tarantoolsession

    tarantool> s = box.space.tester
    tarantool> s:select({1}, {iterator = 'GE'})
    tarantool> s:insert{2, 'Tuple inserted on Terminal #2'}

Now the screen looks like this:

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-5

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-5-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-5-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-5-content

        .. container:: b-documentation_tab
            :name: terminal-5-1

            .. include:: 4-1.rst

        .. container:: b-documentation_tab
            :name: terminal-5-2

            .. include:: 4-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-5 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-5 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-5-1').height(), $('#terminal-5-2').height());
                    $('#catalog-5-content').height(maxHeight + 10);
                    $('#terminal-5-1').height(maxHeight);
                    $('#terminal-5-2').height(maxHeight);
                    $('#terminal-5-1').show();
                    $('#terminal-5-2').hide();
                });
            })();
        </script>

The selection and insertion were successful on Terminal #2. Nothing has
happened on Terminal #1.

On Terminal #1, execute these Tarantool requests and shell commands:

.. code-block:: console

    $ os.exit()
    $ ls -l ~/tarantool_test_node_1
    $ ls -l ~/tarantool_test_node_2

Now Tarantool #1 is stopped. Messages appear on Terminal #2 announcing that fact.
The ``ls -l`` commands show that both servers have made snapshots, which have the
same size because they both contain the same tuples.

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-6

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-6-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-6-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-6-content

        .. container:: b-documentation_tab
            :name: terminal-6-1

            .. include:: 5-1.rst

        .. container:: b-documentation_tab
            :name: terminal-6-2

            .. include:: 5-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-6 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-6 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-6-1').height(), $('#terminal-6-2').height());
                    $('#catalog-6-content').height(maxHeight + 10);
                    $('#terminal-6-1').height(maxHeight);
                    $('#terminal-6-2').height(maxHeight);
                    $('#terminal-6-1').show();
                    $('#terminal-6-2').hide();
                });
            })();
        </script>

On Terminal #2, ignore the repeated messages saying "failed to connect",
and execute these requests:

.. code-block:: tarantoolsession

    tarantool> box.space.tester:select({0}, {iterator = 'GE'})
    tarantool> box.space.tester:insert{3, 'Another'}

Now the screen looks like this (ignoring the repeated messages saying
"failed to connect"):

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-7

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-7-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-7-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-7-content

        .. container:: b-documentation_tab
            :name: terminal-7-1

            .. include:: 6-1.rst

        .. container:: b-documentation_tab
            :name: terminal-7-2

            .. include:: 6-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-7 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-7 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-7-1').height(), $('#terminal-7-2').height());
                    $('#catalog-7-content').height(maxHeight + 10);
                    $('#terminal-7-1').height(maxHeight);
                    $('#terminal-7-2').height(maxHeight);
                    $('#terminal-7-1').show();
                    $('#terminal-7-2').hide();
                });
            })();
        </script>

Terminal #2 has done a select and an insert, even though Terminal #1 is down.

On Terminal #1 execute these commands:

.. code-block:: tarantoolsession

    $ ~/tarantool/src/tarantool
    tarantool> box.cfg{listen = 3301}
    tarantool> box.space.tester:select({0}, {iteratir = 'GE'})

Now the screen looks like this (ignoring the repeated messages on terminal #2
saying "failed to connect"):

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-8

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-8-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-8-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-8-content

        .. container:: b-documentation_tab
            :name: terminal-8-1

            .. include:: 7-1.rst

        .. container:: b-documentation_tab
            :name: terminal-8-2

            .. include:: 7-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-8 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-8 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-8-1').height(), $('#terminal-8-2').height());
                    $('#catalog-8-content').height(maxHeight + 10);
                    $('#terminal-8-1').height(maxHeight);
                    $('#terminal-8-2').height(maxHeight);
                    $('#terminal-8-1').show();
                    $('#terminal-8-2').hide();
                });
            })();
        </script>

The master has reconnected to the cluster, and has NOT found what the replica
wrote while the master was away. That is not a surprise -- the replica has not
been asked to act as a replication source.

On Terminal #1, say:

.. code-block:: tarantoolsession

    tarantool> box.cfg{
             >   replication_source = 'replicator:password@localhost:3302'
             > }
    tarantool> box.space.tester:select({0}, {iterator = 'GE'})

The screen now looks like this:

.. container:: b-block-wrapper_doc

    .. container:: b-doc_catalog
        :name: catalog-9

        .. raw:: html

            <ul class="b-tab_switcher">
                <li class="b-tab_switcher-item">
                    <a href="#terminal-9-1" class="b-tab_switcher-item-url p-active">Terminal #1</a>
                </li>
                <li class="b-tab_switcher-item">
                    <a href="#terminal-9-2" class="b-tab_switcher-item-url">Terminal #2</a>
                </li>
            </ul>

    .. container:: b-documentation_tab_content
        :name: catalog-9-content

        .. container:: b-documentation_tab
            :name: terminal-9-1

            .. include:: 8-1.rst

        .. container:: b-documentation_tab
            :name: terminal-9-2

            .. include:: 8-2.rst

    .. raw:: html

        <script>
            (function(){
                var dOn = $(document);
                dOn.on({
                    click: function(event) {
                        event.preventDefault();
                        link = $(this).children('a');
                        target = link.attr('href');
                        if (!(link.hasClass('p-active'))) {
                            active = $('#catalog-9 .b-tab_switcher-item-url.p-active');
                            $(active.attr('href')).hide();
                            active.removeClass('p-active');
                            link.addClass('p-active');
                            $(link.attr('href')).show();
                        }
                    }
                }, '#catalog-9 .b-tab_switcher-item');
                dOn.ready(function(event) {
                    maxHeight = Math.max($('#terminal-9-1').height(), $('#terminal-9-2').height());
                    $('#catalog-9-content').height(maxHeight + 10);
                    $('#terminal-9-1').height(maxHeight);
                    $('#terminal-9-2').height(maxHeight);
                    $('#terminal-9-1').show();
                    $('#terminal-9-2').hide();
                });
            })();
        </script>

This shows that the two servers are once again in synch, and that each server
sees what the other server wrote.

To clean up, say "``os.exit()``" on both Terminal #1 and Terminal #2, and then
on either terminal say:

.. code-block:: console

    $ cd ~
    $ rm -R ~/tarantool_test_node_1
    $ rm -R ~/tarantool_test_node_2
