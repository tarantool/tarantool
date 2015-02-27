-------------------------------------------------------------------------------
                                  Compiling
-------------------------------------------------------------------------------

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


