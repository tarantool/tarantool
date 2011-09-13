<?php

define("NMSPACE", 0);
define("PRIMARYINDEX", 0);

define("FIELD_1", 1);
define("FIELD_2", 2);
define("FIELD_3", 3);

if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;

// constructor(host,port);
$tnt = new Tarantool('tfn24');

$tnt->select(NMSPACE,PRIMARYINDEX,1);
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
$tnt->inc(NMSPACE,1,FIELD_3);

$tnt->select(NMSPACE,PRIMARYINDEX,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// duble increment (+2)
$tnt->inc(NMSPACE,1,FIELD_3,2);

$tnt->select(NMSPACE,PRIMARYINDEX,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// decrement
$res = $tnt->inc(NMSPACE,1,FIELD_3,-1);
echo "increment tuple res=$res\n";

$tnt->select(NMSPACE,PRIMARYINDEX,1);
$tuple = $tnt->getTuple();
var_dump($tuple);

// error, type inxex must be NUM 
$res = $tnt->inc(NMSPACE,1,FIELD_2);
echo "increment tuple res=$res\n";
if (!$res)
    echo $tnt->getError();
$tnt->select(NMSPACE,PRIMARYINDEX,1);
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
$res = $tnt->update(NMSPACE,1,array(2=>'y'));
echo "update tuple res=$res\n";

$tnt->select(NMSPACE,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


$res = $tnt->update(NMSPACE,1,array(1 => 'z', 2=>'abc'));
echo "update tuple res=$res\n";

$tnt->select(NMSPACE,0,1);
$tuple = $tnt->getTuple();
var_dump($tuple);


// Error  type inxex 0 must be NUM 
$res = $tnt->update(NMSPACE,1,array(0 => 'z', 2=>'abc'));
echo "update tuple res=$res\n";
if (!$res)
    echo $tnt->getError();


