<?php

define("NMSPACE", 0);

if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

/**
 *  Tarantool::constructor
 * 
 *  @param optional string host default 127.0.0.1
 *  @param optional number port default 33013
 */
$tnt = new Tarantool('localhost');

/**
 *  Tarantool::call
 * 
 *  @param procname procedure namei
 *  @param array    procedure argumetns
 * 
 *  @return count of tuples
 */
$tuple = array('test', 'x', 'abd', 1023);

$res = $tnt->insert(NMSPACE, $tuple);

$tuple = array(NMSPACE, 0, 'test');

$res = $tnt->call("box.select", $tuple);

var_dump($res);

while(($res = $tnt->getTuple()) != false) {
    var_dump($res);
}
