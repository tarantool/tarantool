-------------------------------------------------------------------------------
                                Package `tap`
-------------------------------------------------------------------------------

The tap package streamlines the testing of other packages. It allows writing of
tests in the `TAP protocol`_. The results from the tests can be parsed by
standard TAP-analyzers so they can be passed to utilities such as `prove`_. Thus
one can run tests and then use the results for statistics, decision-making, and so on.

.. module:: tap

.. function:: test(test-name)

    Initialize.

    The result of ``tap.test`` is an object, which will be called taptest
    in the rest of this discussion, which is necessary for
    ``taptest:plan()`` and all the other methods.

    :param string test-name: an arbitrary name to give for the test outputs.
    :return: taptest
    :rtype:  userdata

    .. code-block:: lua

        tap = require('tap')
        taptest = tap.test('test-name')

.. class:: taptest

    .. method:: plan(count)

        Indicate how many tests will be performed.

        :param number count:
        :return: nil

    .. method:: check()

        Checks the number of tests performed. This check should only be done
        after all planned tests are complete, so ordinarily ``taptest:check()``
        will only appear at the end of a script.

        Will display ``# bad plan: ...`` if the number of completed tests is not
        equal to the number of tests specified by ``taptest:plan(...)``.

        :return: nil

    .. method:: diag(message)

        Display a diagnostic message.

        :param string message: the message to be displayed.
        :return: nil

    .. method:: ok(condition, test-name)

        This is a basic function which is used by other functions. Depending
        on the value of ``condition``, print 'ok' or 'not ok' along with
        debugging information. Displays the message.

        :param boolean condition: an expression which is true or false
        :param string  test-name: name of test

        :return: true or false.
        :rtype:  boolean

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> taptest:ok(true, 'x')
            ok - x
            ---
            - true
            ...
            tarantool> tap = require('tap')
            ---
            ...
            tarantool> taptest = tap.test('test-name')
            TAP version 13
            ---
            ...
            tarantool> taptest:ok(1 + 1 == 2, 'X')
            ok - X
            ---
            - true
            ...

    .. method:: fail(test-name)

        ``taptest:fail('x')`` is equivalent to ``taptest:ok(false, 'x')``.
        Displays the message.

        :param string  test-name: name of test

        :return: true or false.
        :rtype:  boolean

    .. method:: skip(message)

        ``taptest:skip('x')`` is equivalent to
        ``taptest:ok(true, 'x' .. '# skip')``.
        Displays the message.

        :param string  test-name: name of test

        :return: nil

        **Example:**

        .. code-block:: tarantoolsession

            tarantool> taptest:skip('message')
            ok - message # skip
            ---
            - true
            ...

    .. method:: is(got, expected, test-name)

        Check whether the first argument equals the second argument.
        Displays extensive message if the result is false.

        :param number got: actual result
        :param number expected: expected result
        :param string test-name: name of test
        :return: true or false.
        :rtype:  boolean

    .. method:: isnt(got, expected, test-name)

        This is the negation of ``taptest:is(...)``.

        :param number got: actual result
        :param number expected: expected result
        :param string test-name: name of test

        :return: true of false.
        :rtype:  boolean

    .. method:: isnil(value, test-name)
                isstring(value, test-name)
                isnumber(value, test-name)
                istable(value, test-name)
                isboolean(value, test-name)
                isudata(value, test-name)
                iscdata(value, test-name)

        Test whether a value has a particular type. Displays a long message if
        the value is not of the specified type.

        :param lua-value value:
        :param string test-name: name of test

        :return: true of false.
        :rtype:  boolean

    .. method:: is_deeply(got, expected, test-name)

        Recursive version of ``tap-test:is(...)``, which can be be used to
        compare tables as well as scalar values.

        :return: true of false.
        :rtype:  boolean

        :param lua-value got: actual result
        :param lua-value expected: expected result
        :param string test-name: name of test


.. _prove: https://metacpan.org/pod/distribution/Test-Harness/bin/prove
.. _TAP protocol: https://en.wikipedia.org/wiki/Test_Anything_Protocol

=================================================
                     Example
=================================================

To run this example: put the script in a file named ./tap.lua, then make
tap.lua executable by saying ``chmod a+x ./tap.lua``, then execute using
Tarantool as a script processor by saying ./tap.lua.

.. code-block:: lua

    #!/usr/bin/tarantool
    local tap = require('tap')
    test = tap.test("my test name")
    test:plan(2)
    test:ok(2 * 2 == 4, "2 * 2 is 4")
    test:test("some subtests for test2", function(test)
        test:plan(2)
        test:is(2 + 2, 4, "2 + 2 is 4")
        test:isnt(2 + 3, 4, "2 + 3 is not 4")
    end)
    test:check()

The output from the above script will look approximately like this:

.. code-block:: tap

    TAP version 13
    1..2
    ok - 2 * 2 is 4
        # Some subtests for test2
        1..2
        ok - 2 + 2 is 4,
        ok - 2 + 3 is not 4
        # Some subtests for test2: end
    ok - some subtests for test2
