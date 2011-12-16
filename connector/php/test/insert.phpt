--TEST--
Tarantool/box insert command test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);

echo "---------- test begin ----------\n";
echo "test insert: invalid tuple (expected error exception)\n";
test_insert($tarantool, 0, array(0, array(1, 2, 3), "str"), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: invalid tuple (expected error exception)\n";
test_insert($tarantool, 0, array(0, $tarantool), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: assign tuple";
test_insert($tarantool, 0, $sw4,
            TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: assign tuple (tuple doesn't return)\n";
test_insert($tarantool, 0, $sw6, 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: add existed tuple (expected error exception)\n";
test_insert($tarantool, 0, $sw4,
            TARANTOOL_FLAGS_RETURN_TUPLE | TARANTOOL_FLAGS_ADD);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: replace not existed tuple (expected error exception)\n";
test_insert($tarantool, 0, $sw5,
            TARANTOOL_FLAGS_RETURN_TUPLE | TARANTOOL_FLAGS_REPLACE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: add not existed tuple\n";
test_insert($tarantool, 0, $sw5,
            TARANTOOL_FLAGS_RETURN_TUPLE | TARANTOOL_FLAGS_ADD);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test insert: replace existed tuple\n";
test_insert($tarantool, 0, $sw5,
            TARANTOOL_FLAGS_RETURN_TUPLE | TARANTOOL_FLAGS_REPLACE);
echo "----------- test end -----------\n\n";

test_clean($tarantool, 0);
?>
===DONE===
--EXPECT--
---------- test begin ----------
test insert: invalid tuple (expected error exception)
catched exception: unsupported field type
----------- test end -----------

---------- test begin ----------
test insert: invalid tuple (expected error exception)
catched exception: unsupported field type
----------- test end -----------

---------- test begin ----------
test insert: assign tupleresult:
count = 1
tuple:
  id     = 0
  series = Star Wars
  year   = 1977
  name   = A New Hope
  crawl  = A long time ago, in a galaxy far, far away...
It is a period of civil war. Rebel
spaceships, striking from a hidden
base, have won their first victory
against the evil Galactic Empire.

During the battle, Rebel spies managed
to steal secret plans to the Empire's
ultimate weapon, the Death Star, an
armored space station with enough
power to destroy an entire planet.

Pursued by the Empire's sinister agents,
Princess Leia races home aboard her
starship, custodian of the stolen plans
that can save her people and restore
freedom to the galaxy....
----------- test end -----------

---------- test begin ----------
test insert: assign tuple (tuple doesn't return)
result:
count = 1
----------- test end -----------

---------- test begin ----------
test insert: add existed tuple (expected error exception)
catched exception: insert failed: 14082
----------- test end -----------

---------- test begin ----------
test insert: replace not existed tuple (expected error exception)
catched exception: insert failed: 12546
----------- test end -----------

---------- test begin ----------
test insert: add not existed tuple
result:
count = 1
tuple:
  id     = 1
  series = Star Wars
  year   = 1980
  name   = The Empire Strikes Back
  crawl  = It is a dark time for the
Rebellion. Although the Death
Star has been destroyed.
Imperial troops have driven the
Rebel forces from their hidden
base and pursued them across
the galaxy.

Evading the dreaded Imperial
Starfleet, a group of freedom
fighters led by Luke Skywalker
have established a new secret base
on the remote ice world
of Hoth.

The evil lord Darth Vader,
obsessed with finding young
Skywalker, has dispatched
thousands of remote probes
into the far reaches of space....
----------- test end -----------

---------- test begin ----------
test insert: replace existed tuple
result:
count = 1
tuple:
  id     = 1
  series = Star Wars
  year   = 1980
  name   = The Empire Strikes Back
  crawl  = It is a dark time for the
Rebellion. Although the Death
Star has been destroyed.
Imperial troops have driven the
Rebel forces from their hidden
base and pursued them across
the galaxy.

Evading the dreaded Imperial
Starfleet, a group of freedom
fighters led by Luke Skywalker
have established a new secret base
on the remote ice world
of Hoth.

The evil lord Darth Vader,
obsessed with finding young
Skywalker, has dispatched
thousands of remote probes
into the far reaches of space....
----------- test end -----------

===DONE===