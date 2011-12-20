--TEST--
Tarantool/box administation commands test
--FILE--
<?php
include "lib/php/tarantool_utest.php";

$tarantool = new Tarantool("localhost", 33013, 33015);

echo "---------- test begin ----------\n";
echo "help\n";
echo $tarantool->admin("help");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "insert\n";
for ($i = 0; $i < 10; ++$i)
    echo $tarantool->admin("lua box.insert(0, $i, 'test_id1', 'test field #$i')");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "select\n";
echo $tarantool->admin("lua box.select(0, 1, 'test_id1')");
echo "----------- test end -----------\n\n";

echo "---------- test begin ----------\n";
echo "delete\n";
for ($i = 0; $i < 10; ++$i)
    echo $tarantool->admin("lua box.delete(0, $i)");
echo "----------- test end -----------\n\n";
?>
===DONE===
--EXPECT--
---------- test begin ----------
help
available commands:
 - help
 - exit
 - show info
 - show fiber
 - show configuration
 - show slab
 - show palloc
 - show stat
 - save coredump
 - save snapshot
 - lua command
 - reload configuration
----------- test end -----------

---------- test begin ----------
insert
 - 0: {'test_id1', 'test field #0'}
 - 1: {'test_id1', 'test field #1'}
 - 2: {'test_id1', 'test field #2'}
 - 3: {'test_id1', 'test field #3'}
 - 4: {'test_id1', 'test field #4'}
 - 5: {'test_id1', 'test field #5'}
 - 6: {'test_id1', 'test field #6'}
 - 7: {'test_id1', 'test field #7'}
 - 8: {'test_id1', 'test field #8'}
 - 9: {'test_id1', 'test field #9'}
----------- test end -----------

---------- test begin ----------
select
 - 0: {'test_id1', 'test field #0'}
 - 1: {'test_id1', 'test field #1'}
 - 2: {'test_id1', 'test field #2'}
 - 3: {'test_id1', 'test field #3'}
 - 4: {'test_id1', 'test field #4'}
 - 5: {'test_id1', 'test field #5'}
 - 6: {'test_id1', 'test field #6'}
 - 7: {'test_id1', 'test field #7'}
 - 8: {'test_id1', 'test field #8'}
 - 9: {'test_id1', 'test field #9'}
----------- test end -----------

---------- test begin ----------
delete
 - 0: {'test_id1', 'test field #0'}
 - 1: {'test_id1', 'test field #1'}
 - 2: {'test_id1', 'test field #2'}
 - 3: {'test_id1', 'test field #3'}
 - 4: {'test_id1', 'test field #4'}
 - 5: {'test_id1', 'test field #5'}
 - 6: {'test_id1', 'test field #6'}
 - 7: {'test_id1', 'test field #7'}
 - 8: {'test_id1', 'test field #8'}
 - 9: {'test_id1', 'test field #9'}
----------- test end -----------

===DONE===