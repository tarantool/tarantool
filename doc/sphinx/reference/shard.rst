-------------------------------------------------------------------------------
                            Package `shard`
-------------------------------------------------------------------------------

.. module:: shard

With sharding, the tuples of a tuple set are distributed to multiple nodes,
with a Tarantool database server on each node. With this arrangement,
each server is handling only a subset of the total data,
so larger loads can be handled by simply adding more computers to a network.

The Tarantool shard package has facilities for creating shards,
as well as analogues for the data-manipulation functions of the box library
(select, insert, replace, update, delete).

First some terminology: |br|
**Consistent Hash** ...
The shard package distributes according to a hash algorithm,
that is, it applies a hash function to a tuple's primary-key value
in order to decide which shard the tuple belongs to.
The hash function is `consistent`_
so that changing the number of servers will not affect results for many keys.
The specific hash function that the shard package uses is
guava.digest in the :codeitalic:`digest` package. |br|
**Queue** ...
A temporary list of recent update requests. Sometimes called "batching".
Since updates to a sharded database can be slow, it may
speed up throughput to send requests to a queue rather
than wait for the update to finish on ever node.
The shard package has functions for adding requests to the queue,
which it will process without further intervention.
Queuing is optional. |br|
**Redundancy** ...
The number of replicas in each shard. |br|
**Replica** ...
A complete copy of the data.
The shard package handles both sharding and replication.
One shard can contain one or more replicas.
When a write occurs, the write is attempted on every replica in turn.
The shard package does not use the built-in replication feature. |br|
**Shard** ...
A subset of the tuples in the database partitioned according to the
value returned by the consistent hash function. Usually each shard
is on a separate node, or a separate set of nodes (for example if
redundancy = 3 then the shard will be on three nodes). |br|
**Zone** ...
A physical location where the nodes are closely connected, with
the same security and backup and access points. The simplest example
of a zone is a single computer with a single tarantool-server instance.
A shard's replicas should be in different zones.

The shard package is distributed separately from the main tarantool package.
To acquire it, do a separate install. For example on Ubuntu say |br|
sudo apt-get install tarantool-shard tarantool-pool |br|
Or, download from github tarantool/shard
and compile as described in the README. Then, before using the package, say
shard = require('shard')

The most important function is |br|
:samp:`shard.init ({shard-configuration})` |br|
This must be called for every shard.
The shard-configuration is a table with these fields: |br|
* servers (a list of URIs of nodes and the zones the nodes are in) |br|
* login (the user name which applies for accessing via the shard package) |br|
* password (the password for the login) |br|
* redundancy (a number, minimum 1) |br|
* binary (a port number that this host is listening on, on the current host)
(distinguishable from the 'listen' port specified by box.cfg) |br|
Possible Errors: Redundancy should not be greater than the number of servers;
the servers must be alive; two replicas of the same shard should not be in the same zone.

  | EXAMPLE: shard.init syntax for one shard
  | :codenormal:`-- The number of replicas per shard (redundancy) is 3.`
  | :codenormal:`-- The number of servers is 3.`
  | :codenormal:`-- The shard package will conclude that there is only one shard.`
  | :codenormal:`cfg = {`
  | |nbsp| |nbsp| :codenormal:`servers = {`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33131', zone = '1' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33132', zone = '2' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33133', zone = '3' };`
  | |nbsp| |nbsp| :codenormal:`};`
  | |nbsp| |nbsp| :codenormal:`login = 'tester';`
  | |nbsp| |nbsp| :codenormal:`password = 'pass';`
  | |nbsp| |nbsp| :codenormal:`redundancy = 3;`
  | |nbsp| |nbsp| :codenormal:`binary = 33131;`
  | :codenormal:`}`
  | :codenormal:`server.init(cfg)`
  |
  | :codenormal:`EXAMPLE: shard.init syntax for three shards`
  | :codenormal:`-- This describes three shards.`
  | :codenormal:`-- Each shard has two replicas.`
  | :codenormal:`-- Since the number of servers is 7, and the number`
  | :codenormal:`-- of replicas per server is 2, and dividing 7 / 2`
  | :codenormal:`-- leaves a remainder of 1, one of the servers will`
  | :codenormal:`-- not be used. This is not necessarily an error,`
  | :codenormal:`-- because perhaps one of the servers in the list is`
  | :codenormal:`-- not alive.`
  | :codenormal:`cfg = {`
  | |nbsp| |nbsp| :codenormal:`servers = {`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33131', zone = '1' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33132', zone = '2' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33133', zone = '1' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33134', zone = '2' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33135', zone = '1' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33136', zone = '2' };`
  | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:33137', zone = '1' };`
  | |nbsp| |nbsp| :codenormal:`};`
  | |nbsp| |nbsp| :codenormal:`login = 'tester';`
  | |nbsp| |nbsp| :codenormal:`password = 'pass';`
  | |nbsp| |nbsp| :codenormal:`redundancy = 2;`
  | |nbsp| |nbsp| :codenormal:`binary = 33131;`
  | :codenormal:`}`
  | :codenormal:`server.init(cfg)`

:samp:`shard.{space_name}.insert` :code:`{...}` etc. |br|
Every data-access function in the box package
has an analogue in the shard package, so (for
example) to insert in table T in a sharded database one
simply says "shard.T:insert{...}" instead of
"box.T:insert{...}".
A "shard.T:select{}" request without a primary key will search all shards.
    
:samp:`q_shard.{space_name}.insert` {:code:`{...}` etc. |br|
Every queued data-access function has an analogue in the shard package.
The user must add an operation_id. The details of queued
data-access functions, and of maintenance-related functions,
are on `the shard section of github`_.

    | :codenormal:`EXAMPLE -- Shard, Minimal Configuration`
    | :codenormal:`-- There is only one`
    | :codenormal:`-- shard, and that shard contains only one replica.`
    | :codenormal:`-- So this isn't illustrating the features of either`
    | :codenormal:`-- replication or sharding, it's only illustrating``
    | :codenormal:`-- what the syntax is,`
    | :codenormal:`-- and what the messages look like,`
    | :codenormal:`-- that anyone could duplicate in a minute or two`
    | :codenormal:`-- with the magic of cut-and-paste.`
    | :codenormal:`mkdir ~/tarantool_sandbox_1`
    | :codenormal:`cd ~/tarantool_sandbox_1`
    | :codenormal:`rm -r *.snap`
    | :codenormal:`rm -r *.xlog`
    | :codenormal:`~/tarantool-master/src/tarantool`
    | :codenormal:`box.cfg{listen = 3301}`
    | :codenormal:`box.schema.space.create('tester')`
    | :codenormal:`box.space.tester:create_index('primary', {})`
    | :codenormal:`box.schema.user.passwd('admin', 'password')`
    | :codenormal:`console = require('console')`
    | :codenormal:`console.delimiter('!')`
    | :codenormal:`cfg = {`
    | |nbsp| |nbsp| :codenormal:`servers = {`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:3301', zone = '1' };`
    | |nbsp| |nbsp| :codenormal:`};`
    | |nbsp| |nbsp| :codenormal:`login = 'admin';`
    | |nbsp| |nbsp| :codenormal:`password = 'password';`
    | |nbsp| |nbsp| :codenormal:`redundancy = 1;`
    | |nbsp| |nbsp| :codenormal:`binary = 3301;`
    | :codenormal:`}!`
    | :codenormal:`shard = require('shard')!`
    | :codenormal:`shard.init(cfg)!`
    | :codenormal:`-- Now put something in ...!`
    | :codenormal:`shard.tester:insert{1,'Tuple #1'}!`

If one cuts and pastes the above, then the result,
showing only the requests and responses for shard.init
and shard.tester, should look approximately like this:

    | :codenormal:`tarantool>` :codebold:`shard.init(cfg)!`
    | :codenormal:`2015-08-09 ... I> Sharding initialization started...`
    | :codenormal:`2015-08-09 ... I> establishing connection to cluster servers...`
    | :codenormal:`2015-08-09 ... I>  - localhost:3301 - connecting...`
    | :codenormal:`2015-08-09 ... I>  - localhost:3301 - connected`
    | :codenormal:`2015-08-09 ... I> connected to all servers`
    | :codenormal:`2015-08-09 ... I> started`
    | :codenormal:`2015-08-09 ... I> redundancy = 1`
    | :codenormal:`2015-08-09 ... I> Zone len=1 THERE`
    | :codenormal:`2015-08-09 ... I> Adding localhost:3301 to shard 1`
    | :codenormal:`2015-08-09 ... I> Zone len=1 THERE`
    | :codenormal:`2015-08-09 ... I> shards = 1`
    | :codenormal:`2015-08-09 ... I> Done`
    | :codenormal:`---`
    | :codenormal:`- true`
    | :codenormal:`...`
    |
    | :codenormal:`tarantool>` :codebold:`-- Now put something in ...!`
    | :codenormal:`---`
    | :codenormal:`...`
    |
    | :codenormal:`tarantool>` :codebold:`shard.tester:insert{1,'Tuple #1'}!`
    | :codenormal:`---`
    | :codenormal:`- - [1, 'Tuple #1']`
    | :codenormal:`...`




    |
    |
    | :codenormal:`EXAMPLE -- Shard, Scaling Out`
    | :codenormal:`-- There are two shards, and each shard contains one replica.`
    | :codenormal:`-- This requires two nodes. In real life the two nodes would`
    | :codenormal:`-- be two computers, but for this illustration the requirement`
    | :codenormal:`-- is merely: start two shells, which we'll call Terminal#1 and`
    | :codenormal:`-- Terminal #2.`
    |
    | :codenormal:`-- On Terminal #1, say:`
    | :codenormal:`mkdir ~/tarantool_sandbox_1`
    | :codenormal:`cd ~/tarantool_sandbox_1`
    | :codenormal:`rm -r *.snap`
    | :codenormal:`rm -r *.xlog`
    | :codenormal:`~/tarantool-master/src/tarantool`
    | :codenormal:`box.cfg{listen = 3301}`
    | :codenormal:`box.schema.space.create('tester')`
    | :codenormal:`box.space.tester:create_index('primary', {})`
    | :codenormal:`box.schema.user.passwd('admin', 'password')`
    | :codenormal:`console = require('console')`
    | :codenormal:`console.delimiter('!')`
    | :codenormal:`cfg = {`
    | |nbsp| |nbsp| :codenormal:`servers = {`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:3301', zone = '1' };`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:3302', zone = '2' };`
    | |nbsp| |nbsp| :codenormal:`};`
    | |nbsp| |nbsp| :codenormal:`login = 'admin';`
    | |nbsp| |nbsp| :codenormal:`password = 'password';`
    | |nbsp| |nbsp| :codenormal:`redundancy = 1;`
    | |nbsp| |nbsp| :codenormal:`binary = 3301;`
    | |nbsp| |nbsp| :codenormal:`}!`
    | |nbsp| |nbsp| :codenormal:`shard = require('shard')!`
    | |nbsp| |nbsp| :codenormal:`shard.init(cfg)!`
    | |nbsp| |nbsp| :codenormal:`-- Now put something in ...!`
    | |nbsp| |nbsp| :codenormal:`shard.tester:insert{1,'Tuple #1'}!`
    |
    | -- On Terminal #2, say:
    | :codenormal:`mkdir ~/tarantool_sandbox_2`
    | :codenormal:`cd ~/tarantool_sandbox_2`
    | :codenormal:`rm -r *.snap`
    | :codenormal:`rm -r *.xlog`
    | :codenormal:`~/tarantool-master/src/tarantool`
    | :codenormal:`box.cfg{listen = 3302}`
    | :codenormal:`box.schema.space.create('tester')`
    | :codenormal:`box.space.tester:create_index('primary', {})`
    | :codenormal:`box.schema.user.passwd('admin', 'password')`
    | :codenormal:`console = require('console')`
    | :codenormal:`console.delimiter('!')`
    | :codenormal:`cfg = {`
    | |nbsp| |nbsp| :codenormal:`servers = {`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:3301', zone = '1' };`
    | |nbsp| |nbsp| |nbsp| |nbsp| :codenormal:`{ uri = 'localhost:3302', zone = '2' };`
    | |nbsp| |nbsp| :codenormal:`};`
    | |nbsp| |nbsp| :codenormal:`login = 'admin';`
    | |nbsp| |nbsp| :codenormal:`password = 'password';`
    | |nbsp| |nbsp| :codenormal:`redundancy = 1;`
    | |nbsp| |nbsp| :codenormal:`binary = 3302;`
    | :codenormal:`}!`
    | :codenormal:`shard = require('shard')!`
    | :codenormal:`shard.init(cfg)!`
    | :codenormal:`-- Now get something out ...!`
    | :codenormal:`shard.tester:select{1}!`

What will appear on Terminal #1 is: a loop of
error messages saying "Connection refused" and
"server check failure". This is normal. It will
go on until Terminal #2 process starts.

What will appear on Terminal #2, at the end,
should look like this:

| :codenormal:`tarantool>shard.tester:select{1}!`
| :codenormal:`---`
| :codenormal:`- - - [1, 'Tuple #1']`
| :codenormal:`...`

This shows that what was inserted by Terminal #1
can be selected by Terminal #2, via the shard package.

Details are on `the shard section of github`_.

.. _consistent: https://en.wikipedia.org/wiki/Consistent_hashing
.. _the shard section of github: https://github.com/tarantool/shard

