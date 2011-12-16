--TEST--
Tarantool/box select commands test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);
test_init($tarantool, 0);

echo "---------- test begin ----------\n";
echo "test select: key is integer\n";
test_select($tarantool, 0, 0, 0);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is string \n";
test_select($tarantool, 0, 1, "Star Wars");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is object (expected error exception)\n";
test_select($tarantool, 0, 0, $tarantool);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is array\n";
test_select($tarantool, 0, 1, array("Star Wars"));
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is empty array (expected error exception)\n";
test_select($tarantool, 0, 0, array());
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is array of objects (expected error exception)\n";
test_select($tarantool, 0, 0, array($tarantool, 1, 2));
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: multi keys select\n";
test_select($tarantool, 0, 0, array(array(0), array(1), array(2)));
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is array of array of objects (expected error exception)\n";
test_select($tarantool, 0, 0, array(array(1, $tarantool)));
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test select: key is array of diffirent elements (expected error exception)\n";
test_select($tarantool, 0, 0, array(array(1, 2), 2));
echo "----------- test end -----------\n\n";

test_clean($tarantool, 0);
?>
===DONE===
--EXPECT--
---------- test begin ----------
test select: key is integer
result:
count = 1
tuple[0]:
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
test select: key is string 
result:
count = 3
tuple[0]:
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
tuple[1]:
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
tuple[2]:
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
test select: key is object (expected error exception)
catched exception: unsupported tuple type
----------- test end -----------

---------- test begin ----------
test select: key is array
result:
count = 3
tuple[0]:
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
tuple[1]:
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
tuple[2]:
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
test select: key is empty array (expected error exception)
catched exception: invalid tuples list: empty array
----------- test end -----------

---------- test begin ----------
test select: key is array of objects (expected error exception)
catched exception: unsupported tuple type
----------- test end -----------

---------- test begin ----------
test select: multi keys select
result:
count = 3
tuple[0]:
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
tuple[1]:
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
tuple[2]:
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
test select: key is array of array of objects (expected error exception)
catched exception: unsupported field type
----------- test end -----------

---------- test begin ----------
test select: key is array of diffirent elements (expected error exception)
catched exception: invalid tuples list: expected array of array
----------- test end -----------

===DONE===