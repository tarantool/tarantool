-------------------------------------------------------------------------------
                             Preface
-------------------------------------------------------------------------------

Welcome to Tarantool. This is the User Guide.
We recommend reading this first, and afterwards
reading the Reference Manual which has more detail.

===============================================================================
                            How to read the documentation
===============================================================================

To get started, one can either download the whole package
as described in the first part of Chapter 2 "Getting started",
or one can initially skip the download and connect to the online
Tarantool server running on the web at http://try.tarantool.org.
Either way, the first tryout can be a matter of following the example
in the second part of chapter 2: "Starting Tarantool and making your first database".

Chapter 3 "Database" is about the Tarantool NoSQL DBMS.
If the only intent is to use Tarantool as a Lua application server,
most of the material in this chapter and in the following chapter
(Chapter 4 "Replication") will not be necessary.
Once again, the detailed instructions about each package can be regarded as reference material.

Chapter 6 "Server administration" and Chapter 5 "Configuration reference"
are primarily for administrators; however, every user should know something
about how the server is configured so the section about box.cfg is not skippable.
Chapter 7 "Connectors" is strictly for users who are connecting from a different
language such as C or Perl or Python -- other users will find no immediate need for this chapter.

The two long tutorials in Appendix C -- "Insert one million tuples with a Lua stored procedure"
and "Sum a JSON field for all tuples" -- start slowly and contain commentary that is especially
aimed at users who may not consider themselves experts at either Lua or NoSQL database management.

Finally, Appendix D "Modules" has examples that will be essential for those users who want to
connect the Tarantool server to another DBMS: MySQL or PostgreSQL.

For experienced users, there is also a reference manual plus developer's guide,
and an extensive set of comments in the source code. 

===============================================================================
             Getting In Touch With The Tarantool Community
===============================================================================

Please report bugs or make feature requests at `http://github.com/tarantool/tarantool/issues`_.

You can contact developers directly on the `#tarantool` IRC channel on freenode.net,
or via a mailing list, `Tarantool Google group`_.

There is also a `Forum for Russian speakers`_.

.. _#tarantool: irc://irc.freenode.net#tarantool
.. _http://github.com/tarantool/tarantool/issues: http://github.com/tarantool/tarantool/issues
.. _Tarantool Google group: https://groups.google.com/forum/#!forum/tarantool
.. _Forum for Russian speakers: https://googlegroups.com/group/tarantool-ru


