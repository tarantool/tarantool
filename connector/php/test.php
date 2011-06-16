<?php

if(!extension_loaded('tarantool')) {
	dl('tarantool.so');
}

$res = true;
//$tnt = new Tarantool( [ $host, $port ]);

$tnt = new Tarantool( 'tfn24');

//$tuple = array(1, 'z','abc', 1025 );
//$tnt->insert(0,$tuple);
exit;

$tuple = array(2, 'z','abd', 1025 );
$tnt->insert(0,$tuple);
$tuple = array(3, 'z','abc', 1025 );
$tnt->insert(0,$tuple);
$tuple = array(4, 'y','abd', 1025 );
$tnt->insert(0,$tuple);
$tuple = array(5, 'y','abc', 1025 );
$tnt->insert(0,$tuple);
$tuple = array(6, 'x','abd', 1025 );
$tnt->insert(0,$tuple);
$tuple = array(6, 'x','abc', 1025 );
$tnt->insert(0,$tuple);


// $count = $tnt->select( 0,0, 1, [10,10] ); // ns, idx , tuple, limit, offset
$count = $tnt->select( 0,0, 1); // ns, idx , 

echo "count tuples $count\n";

$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}


$count = $tnt->select( 0,1, 'x'); // ns, idx , 

echo "count tuples $count\n";

$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}


$count = $tnt->select( 0,2, array('x','abc')); // ns, idx , 

echo "count tuples $count\n";

$i=0;
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}


echo "delete tuple\n";

$tnt->delete(0,1);

$count = $tnt->select( 0,0, 1); 
echo "count tuples $count\n";

echo "incremental tuple\n";

$res = $tnt->inc(0,2,3); 
var_dump($res);  

$count = $tnt->select( 0,0,2); // ns, idx , 
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}
 echo "incremental tuple+2\n";
$res = $tnt->inc(0,2,3,3);
var_dump($res);  

$count = $tnt->select( 0,0,2); // ns, idx , 
while ( false != ($res = $tnt->getTuple())) {    
    var_dump($res);  
}
 
 
