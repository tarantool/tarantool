<?php
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
$res = $tnt->select(0,0,1); // namespace,index, key
var_dump($res);
$res = $tnt->getTuple();
var_dump($res);


echo "some tuple select by fielNo[1] = 'x'\n";
$res = $tnt->select(0,1,'x'); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 2  (fielNo[1] = 'x' and fielNo[2] = 'abc')\n";
$res = $tnt->select(0,2,array('x', 'abc')); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025)\n";
$res = $tnt->select(0,3,1025); // namespace,index, key
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025) LIMIT=3\n";
$res = $tnt->select(0,3,1025,3); // namespace,index, key, limit
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

echo "some tuple select by index = 3  (fielNo[3] = 1025) NEXT 3 (offset=3, limit=3)\n";
$res = $tnt->select(0,3,1025,3,3); // namespace,index, key, limit, offset
var_dump($res);

while( ($res = $tnt->getTuple()) != false){
    var_dump($res);
}

