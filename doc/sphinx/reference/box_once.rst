.. _box-once:

-------------------------------------------------------------------------------
                             Function `box.once`
-------------------------------------------------------------------------------

:codenormal:`box.`:codebold:`once`:codenormal:`(`:codeitalic:`key, function`:codenormal:`)`

Execute a function, provided it has not been executed before.
A passed value is checked to see whether the function has already
been executed. If it has been executed before, nothing happens.
If it has not been executed before, the function is invoked.
For an explanation why ``box.once`` is useful, see the section
:ref:`Preventing Duplicate Actions <preventing-duplicate-actions>`.

Parameters: :codebold:`key` (:codeitalic:`string`) = a value that will be checked,
:codebold:`function` = a function name.
