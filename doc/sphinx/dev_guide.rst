:title: Tarantool Developer Guide
:slug: dev_guide
:save_as: doc/dev_guide.html
:template: documentation_rst

-------------------------------------------------------------------------------
                         Tarantool Developer Guide
-------------------------------------------------------------------------------


===========================================================
              What documentation there is
===========================================================

Tarantool documentation consists of:

* a user guide
* developer guide (you're reading it)
* protool description doc/www/content/doc/box-protocol.rst
* coding stryle guide for Lua, C++, C, Python (for other connectors,
  we use conventions of the connector programming language community)

===========================================================
                        Compiling
===========================================================

===========================================================
                How to build the XML manual
===========================================================

To build XML manual, you'll need:

* xsltproc
* docbook5-xml
* docbook-xsl-ns
* w3c-sgml-lib
* libsaxon-java (for saxon processing)
* libxml-commons-resolver1.1-java
* libxml2-utils
* libxerces2-java
* libxslthl-java
* lynx
* jing

When all pre-requisites are met, you shhould run:

.. code-block:: bash

    $ cmake . -DENABLE_DOC=YES

to enable documentation builder.

If you want to make tarantool user guide, you should run the
following command from tarantool root directory:

.. code-block:: bash

    $ make html

or

.. code-block:: bash

    $ cd doc/user
    $ make

The html version of the user guide will be genreated in doc/www/content/doc

===========================================================
                    Release management
===========================================================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
          How to make a minor release
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: bash

    $ git tag -a 1.4.4 -m "Next minor in 1.4 series"
    $ vim CMakeLists.txt # edit CPACK_PACKAGE_VERSION_PATCH
    $ git push --tags

Update the Web site in doc/www

Update all issues, upload the ChangeLog based on ``git log`` output.
The ChangeLog must only include items which are mentioned as issues
on github. If anything significant is there, which is not mentioned,
something went wrong in release planning and the release should be
held up until this is cleared.

Click 'Release milestone'. Create a milestone for the next minor release.
Alert the driver to target bugs and blueprints to the new milestone.

===========================================================
                    Developer guidelines
===========================================================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
          How to work on a bug
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Any defect, even minor, if it changes the user-visible server behavior, needs
a bug report. Report a bug at http://github.com/tarantool/tarantool/issues.

When reporting a bug, try to come up with a test case right away. Set the
current maintenance milestone for the bug fix, and specify the series.
Assign the bug to yourself. Put the status to 'In progress' Once the patch is
ready, put the bug the bug to 'In review' and solicit a review for the fix.

Once there is a positive code review, push the patch and set the status to 'Closed'

Patches for bugs should contain a reference to the respective Launchpad bug page or
at least bug id. Each patch should have a test, unless coming up with one is
difficult in the current framework, in which case QA should be alerted.

There are two things you need to do when your patch makes it into the master:

* put the bug to 'fix committed',
* delete the remote branch.
