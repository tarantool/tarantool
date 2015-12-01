-------------------------------------------------------------------------------
                                Package `box`
-------------------------------------------------------------------------------

The contents of the ``box`` library can be inspected at runtime
with ``box``, with no arguments. The packages inside the box library are:
``box.schema``, ``box.tuple``, ``box.space``, ``box.index``, ``net.box``,
``box.cfg``, ``box.info``, ``box.slab``, ``box.stat``.
Every package contains one or more Lua functions. A few packages contain
members as well as functions. The functions allow data definition (create
alter drop), data manipulation (insert delete update upsert select replace), and
introspection (inspecting contents of spaces, accessing server configuration).

.. toctree::
    :maxdepth: 1

    /book/box/box_schema
    /book/box/box_space
    /book/box/box_index
    /book/box/box_session
    /book/box/box_tuple
    /book/box/box_introspection
    /book/box/triggers
