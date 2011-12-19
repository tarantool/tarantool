--TEST--
Tarantool/box update fields commands test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);
test_init($tarantool, 0);

echo "---------- test begin ----------\n";
echo "test update fields: do update w/o operations (expected error exception)\n";
test_update_fields($tarantool, 0, 0, array(), TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test update fields: invalid operation list (expected error exception)\n";
test_update_fields($tarantool, 0, 0, array($tarantool), TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "test update fields: do update arith operation\n";
test_update_fields($tarantool, 0, 0,
                   array(
                       array(
                           "field" => 2,
                           "op" => TARANTOOL_OP_ADD,
                           "arg" => 30,
                           ),
                       array(
                           "field" => 0,
                           "op" => TARANTOOL_OP_ASSIGN,
                           "arg" => 5,
                           ),
                       array(
                           "field" => 1,
                           "op" => TARANTOOL_OP_ASSIGN,
                           "arg" => "",
                           ),
                       array(
                           "field" => 3,
                           "op" => TARANTOOL_OP_ASSIGN,
                           "arg" => "return",
                           ),
                       array(
                           "field" => 4,
                           "op" => TARANTOOL_OP_SPLICE,
                           "offset" => 1,
                           "length" => 64,
                           "list" => " <<splice string>> ",
                           ),
                       ), TARANTOOL_FLAGS_RETURN_TUPLE);
echo "----------- test end -----------\n\n";


test_clean($tarantool, 0);
?>
--EXPECT--
---------- test begin ----------
test update fields: do update w/o operations (expected error exception)
catched exception: update fields failed: 514
----------- test end -----------

---------- test begin ----------
test update fields: invalid operation list (expected error exception)
catched exception: invalid operations list
----------- test end -----------

---------- test begin ----------
test update fields: do update arith operation
result:
count = 1
tuple:
  id     = 5
  series = 
  year   = 2007
  name   = return
  crawl  = A <<splice string>> ivil war. Rebel
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