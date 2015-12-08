.. _book-proctitle:

-------------------------------------------------------------------------------
                        Appendix B. Process title
-------------------------------------------------------------------------------

Linux and FreeBSD operating systems allow a running process to modify its title,
which otherwise contains the program name. Tarantool uses this feature to help
meet the needs of system administration, such as figuring out what services are
running on a host, their status, and so on.

A Tarantool server's process title has these components:

.. cssclass:: highlight
.. parsed-literal::

    **program_name** [**initialization_file_name**] **<role_name>** [**custom_proc_title**]

* **program_name** is typically "tarantool".
* **initialization_file_name** is the name of an :ref:`initialization file <init-label>` if one was specified.
* **role_name** is:
  - "running" (ordinary node "ready to accept requests"),
  - "loading" (ordinary node recovering from old snap and wal files),
  - "orphan" (not in a cluster),
  - "hot_standby" (see section :ref:`local hot standby <book-cfg-local_hot_standy>`), or
  - "dumper" + process-id (saving a snapshot).
* **custom_proc_title** is taken from the :confval:`custom_proc_title` configuration parameter, if one was specified.

For example:

.. code-block:: console

    $ ps -AF | grep tarantool
    1000     17337 16716  1 91362  6916   0 11:07 pts/5    00:00:13 tarantool script.lua <running>

