-------------------------------------------------------------------------------
                            Package `shard`
-------------------------------------------------------------------------------

.. module:: shard

With `sharding`_,
the tuples of a tuple set are distributed
to multiple nodes, with a Tarantool database server on each node. With this arrangement,
each server is handling only a subset of the total data, so larger loads can be
handled by simply adding more computers to a network.

The Tarantool shard package has facilities
for creating or redistributing or cleaning up shards, as well as analogues for the
data-manipulation functions of the box library (select, insert, replace, update, delete).

Some details are `On the shard section of github`_.


.. _sharding: https://en.wikipedia.org/wiki/Sharding
.. _On the shard section of github: https://github.com/tarantool/shard


