<?php
if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;

// constructor(host,port);
$tnt = new Tarantool('tfn24');

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);

/**
 *  Tarantool::inc
 * 
 *  @param namespace
 *  @param primary key
 *  @param field No for increment
 *  @param optional incremental data default +1
 * 
 *  @return bool result
 */
// increment
$tnt->inc(0,1,3);

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// duble increment (+2)
$tnt->inc(0,1,3,2);

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// decrement
$res = $tnt->inc(0,1,3,-1);
echo "increment tuple res=$res\n";

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);

// error, type inxex must be NUM 
$res = $tnt->inc(0,1,2);
echo "increment tuple res=$res\n";
if (!$res)
    echo $tnt->getError();
$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);

/**
 *  Tarantool::update
 * 
 *  @param namespace
 *  @param primary key
 *  @param array of new data:
 *          array( fieldNo => newValue, ... )
 * 
 *  @return bool result
 */
$res = $tnt->update(0,1,array(2=>'y'));
echo "update tuple res=$res\n";

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


$res = $tnt->update(0,1,array(1 => 'z', 2=>'abc'));
echo "update tuple res=$res\n";

$tnt->select(0,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// Error  type inxex 0 must be NUM 
$res = $tnt->update(0,1,array(0 => 'z', 2=>'abc'));
echo "update tuple res=$res\n";
if (!$res)
    echo $tnt->getError();


