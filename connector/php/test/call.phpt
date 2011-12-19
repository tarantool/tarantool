--TEST--
Tarantool/box call commands test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);
test_init($tarantool, 0);

echo "---------- test begin ----------\n";
echo "test call: myselect by primary index\n";
test_call($tarantool, "box.select", array(0, 0, 2), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: function w/o params\n";
test_call($tarantool, "myfoo", array(), 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test call: call undefined function (expected error exception)\n";
test_call($tarantool, "fafagaga", array("fafa-gaga", "foo", "bar"), 0);
echo "----------- test end -----------\n\n";

test_clean($tarantool, 0);
?>
===DONE===
--EXPECT--
---------- test begin ----------
test call: myselect by primary index
result:
count = 1
tuple[0]:
  id     = 2
  series = Star Wars
  year   = 1983
  name   = Return of the Jedi
  crawl  = Luke Skywalker has returned
to his home planet of
Tatooine in an attempt
to rescue his friend
Han Solo from the
clutches of the vile
gangster Jabba the Hutt.

Little does Luke know
that the GALACTIC EMPIRE
has secretly begun construction
on a new armored space station
even more powerful than the
first dreaded Death Star.

When completed, this ultimate
weapon will spell certain
doom for the small band of
rebels struggling to restore
freedom to the galaxy...
----------- test end -----------

---------- test begin ----------
test call: function w/o params
result:
count = 1
tuple[0]:
  id     = 0
  series = Star Wars
  year   = 1977 year
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
test call: call undefined function (expected error exception)
catched exception: call failed: 12802
----------- test end -----------

===DONE===