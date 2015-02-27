-------------------------------------------------------------------------------
                               Release management
-------------------------------------------------------------------------------
===========================================================
              How to make a minor release
===========================================================

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
