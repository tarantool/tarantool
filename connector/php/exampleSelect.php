<?php

define("NMSPACE", 0);
define("PRIMARYINDEX", 0);
define("INDEX_1", 1);
define("INDEX_2", 2);
define("INDEX_3", 3);


if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;

/**
 *  Tarantool::constructor
 * 
 *  @param optional string host default 127.0.0.1
 *  @param optional number port default 33013
 */
$tnt = new Tarantool('tfn24');

echo 'single tuple select by primary key (key=1)', PHP_EOL;

/**
 *  Tarantool::select
 * 
 *  @param number namespace
 *  @param number index No 
 *  @param mixed key 
 *  @param optional limit default all
 *  @param optional offset default 0
 * 
 *  @return count of tuples
 */
$res = $tnt->select(NMSPACE,PRIMARYINDEX,1); // namespace,index, key
var_dump($res);
$res = $tnt->getTuple();
var_dump($res);


echo "some tuple select by fielNo[1] = 'x'\n";
$res = $tnt->select(NMSPACE,INDEX_1,'x'); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 2  (fielNo[1] = 'x' and fielNo[2] = 'abc')\n";
$res = $tnt->select(NMSPACE,INDEX_2,array('x', 'abc')); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025)\n";
$res = $tnt->select(NMSPACE,INDEX_3,1025); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025) LIMIT=3\n";
$res = $tnt->select(NMSPACE,INDEX_3,1025,3); // namespace,index, key, limit
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025) NEXT 3 (offset=3, limit=3)\n";
$res = $tnt->select(NMSPACE,INDEX_3,1025,3,3); // namespace,index, key, limit, offset
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

// multi select
/// SELECT * FROM t0 WHERE k0 IN (1,2) 
$count = $tnt->mselect(NMSPACE,PRIMARYINDEX, array(1,2)); // ns, idx , keys, [limit, offset]  
print "count=$count\n";
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}

/// SELECT * FROM t0 WHERE k1 IN ('x','z') 
$count = $tnt->mselect(NMSPACE,INDEX_1, array('x','z')); // ns, idx , keys, [limit, offset]
print "count=$count\n";
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}




