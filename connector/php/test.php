<?php

if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;
$tnt = new Tarantool('tfn24');


$tuple = array( 'z', 1025 );
//$tnt->insert(0,$tuple);

$tuple = 'abcds';

$tuple = array('b');

$count = $tnt->select( 0,0, 'a'); // ns, idx , 

echo "count tuples $count\n";

$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
//    echo ord($res[1]);
    echo "----\n";
}

echo "==============\n";
$count = $tnt->select( 0,1, 'abc'); // ns, idx , 

echo "count tuples $count\n";
//
$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
//    echo ord($res[1]);
    echo "----\n";
}

unset($tnt);
echo "******\n";

?>
