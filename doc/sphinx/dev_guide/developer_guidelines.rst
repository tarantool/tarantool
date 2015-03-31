-------------------------------------------------------------------------------
                              Developer guidelines
-------------------------------------------------------------------------------

===========================================================
                   How to work on a bug
===========================================================

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
