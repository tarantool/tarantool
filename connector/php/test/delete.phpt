--TEST--
Tarantool/box delete commands test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);

test_init($tarantool, 0);

echo "---------- test begin ----------\n";
echo "test delete: invalid key (expected error exception)\n";
test_delete($tarantool, 0, $tarantool, 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test delete: invalid key (expected error exception)\n";
test_delete($tarantool, 0, array($tarantool), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test delete: invalid key (expected error exception)\n";
test_delete($tarantool, 0, array(1, 2), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test delete: delete key as interger\n";
test_delete($tarantool, 0, 0, TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test delete: delete key as array\n";
test_delete($tarantool, 0, array(1), TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test delete: delete key (tuple doesn't return)\n";
test_delete($tarantool, 0, 2, 0);
echo "----------- test end -----------\n\n";

test_clean($tarantool, 0);
?>
===DONE===
--EXPECT--
---------- test begin ----------
test delete: invalid key (expected error exception)
catched exception: unsupported tuple type
----------- test end -----------

---------- test begin ----------
test delete: invalid key (expected error exception)
catched exception: unsupported field type
----------- test end -----------

---------- test begin ----------
test delete: invalid key (expected error exception)
catched exception: delete failed: 514(0x00000202): Illegal parameters, key must be single valued
----------- test end -----------

---------- test begin ----------
test delete: delete key as interger
result:
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
test delete: delete key as array
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
test delete: delete key (tuple doesn't return)
result:
count = 1
----------- test end -----------

===DONE===