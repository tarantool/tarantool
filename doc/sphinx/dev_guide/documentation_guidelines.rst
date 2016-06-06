-------------------------------------------------------------------------------
                        Documentation guidelines
-------------------------------------------------------------------------------

These guidelines are updated on the on-demand basis, covering only those issues
that cause pains to the existing writers. At this point, we do not aim to come
up with an exhaustive Documentation Style Guide for the Tarantool project. 

===========================================================
                        Markup issues
===========================================================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                Wrapping text
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The limit is 80 characters per line for plain text, and no limit for any other
constructions when wrapping affects ReST readability and/or HTML output. Also,
it makes no sense to wrap text into lines shorter than 80 characters unless you
have a good reason to do so.

The 80-character limit comes from the ISO/ANSI 80x24 screen resolution, and it's
unlikely that readers/writers will use 80-character consoles. Yet it's still a
standard for many coding guidelines (including Tarantool). As for writers, the
benefit is that an 80-character page guide allows keeping the text window rather
narrow most of the time, leaving more space for other applications in a
wide-screen environment.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
              Making comments
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Sometimes we may need to leave comments in a ReST file. To make sphinx ignore
some text during processing, use the following per-line notation with ".. //" as
the comment marker:

.. // your comment here

`The notation example is excluded from HTML output, so please see the source in
ReST.`

The starting symbols ".. //" do not interfere with the other ReST markup, and
they are easy to find both visually and using grep. There are no symbols to
escape in grep search, just go ahead with something like this:

  .. code-block:: console

    grep ".. //" doc/sphinx/dev_guide/*.rst

These comments don't work properly in nested documentation, though (e.g. if you
leave a comment in module -> object -> method, sphinx ignores the comment and
all nested content that follows in the method description).

===========================================================
                Language and style issues
===========================================================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
               US vs British spelling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

We use English US spelling.

===========================================================
               Examples and templates
===========================================================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
               Module and function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here is an example of documenting a module (``my_fiber``) and a function 
(``my_fiber.create``).

.. module:: my_fiber

.. function:: create(function [, function-arguments])

    Create and start a ``my_fiber`` object. The object is created and begins to
    run immediately.

    :param function: the function to be associated with the ``my_fiber`` object
    :param function-arguments: what will be passed to function

    :return: created ``my_fiber`` object    
    :rtype: userdata

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> my_fiber = require('my_fiber')
        ---
        ...
        tarantool> function function_name()
                 >   my_fiber.sleep(1000)
                 > end
        ---
        ...
        tarantool> my_fiber_object = my_fiber.create(function_name)
        ---
        ...

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
               Module, class and method
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Here is an example of documenting a module (``my_box.index``), a class
(``my_index_object``) and a function (``my_index_object.rename``).

.. module:: my_box.index

.. class:: my_index_object
    
    .. method:: rename(index-name)

        Rename an index.
                      
        :param index_object: an object reference
        :param index_name: a new name for the index (type = string)

        :return: nil

        Possible errors: index_object does not exist.

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> box.space.space55.index.primary:rename('secondary')
            ---
            ...

        Complexity Factors: Index size, Index type, Number of tuples accessed.
