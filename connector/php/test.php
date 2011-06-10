<?php

if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;
//$tnt = new Tarantool( [ $host, $port ]);

$tnt = new Tarantool( );

$tuple = array( 'z', 1025 );
$tnt->insert(0,$tuple);

// $count = $tnt->select( 0,0, 'a', [10,10] ); // ns, idx , tuple, limit, offset
$count = $tnt->select( 0,0, 'a'); // ns, idx , 

echo "count tuples $count\n";

$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}

echo "delete tuple\n";

$tnt->delete(0,'z');

$count = $tnt->select( 0,0, 'z'); 
echo "count tuples $count\n";
