<?php
if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$tnt = new Tarantool('tfn24');
$res=0;

/**
 *  Tarantool::insert
 * 
 *  @param namespace
 *  @param array of tuple
 * 
 *  @return result
 */

$tuple = array(1, 'x','abd', 1023 );
$res += $tnt->insert(0,$tuple);


$tuple = array(2, 'z','abd', 1023 );
$res = $tnt->insert(0,$tuple);

$tuple = array(3, 'z','abc', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(4, 'y','abd', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(5, 'y','abc', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(6, 'x','abd', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(7, 'x','abc', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(8, 'x','abd', 1025 );
$res += $tnt->insert(0,$tuple);

$tuple = array(9, 'x','abc', 1024 );
$res += $tnt->insert(0,$tuple);

$tuple = array(9, 'x','abc', 1024 );
$res += $tnt->insert(0,$tuple);
echo "inserted $res tuples\n";

$tuple = array('9', 66,65, 1024 );
$res = $tnt->insert(0,$tuple);
if (!$res)
    echo $tnt->getError();
echo "inserted $res tuples\n";

$tuple = array(10, 66,65, 'xdf' );
$res = $tnt->insert(0,$tuple);
if (!$res)
    echo $tnt->getError();
echo "inserted $res tuples\n";