<?php

$sw4 = array(0, "Star Wars", 1977, "A New Hope",
             "A long time ago, in a galaxy far, far away...\n" .
             "It is a period of civil war. Rebel\n" .
             "spaceships, striking from a hidden\n" .
             "base, have won their first victory\n" .
             "against the evil Galactic Empire.\n" .
             "\n" .
             "During the battle, Rebel spies managed\n" .
             "to steal secret plans to the Empire's\n" .
             "ultimate weapon, the Death Star, an\n" . 
             "armored space station with enough\n". 
             "power to destroy an entire planet.\n" .
             "\n" .
             "Pursued by the Empire's sinister agents,\n" .
             "Princess Leia races home aboard her\n" .
             "starship, custodian of the stolen plans\n" .
             "that can save her people and restore\n" .
             "freedom to the galaxy....",
             0xf10dbeef0001);

$sw5 = array(1, "Star Wars", 1980, "The Empire Strikes Back",
             "It is a dark time for the\n" . 
             "Rebellion. Although the Death\n" . 
             "Star has been destroyed.\n" .
             "Imperial troops have driven the\n" . 
             "Rebel forces from their hidden\n" .
             "base and pursued them across\n" . 
             "the galaxy.\n" .
             "\n" .
             "Evading the dreaded Imperial\n" .
             "Starfleet, a group of freedom\n" .
             "fighters led by Luke Skywalker\n" .
             "have established a new secret base\n" .
             "on the remote ice world\n" .
             "of Hoth.\n" .
             "\n" .
             "The evil lord Darth Vader,\n" .
             "obsessed with finding young\n" .
             "Skywalker, has dispatched\n" .
             "thousands of remote probes\n" .
             "into the far reaches of space....",
             0xf10dbeef0002);

$sw6 = array(2, "Star Wars", 1983, "Return of the Jedi",
             "Luke Skywalker has returned\n" .
             "to his home planet of\n" .
             "Tatooine in an attempt\n" .
             "to rescue his friend\n" .
             "Han Solo from the\n" .
             "clutches of the vile\n" .
             "gangster Jabba the Hutt.\n" .
             "\n" .
             "Little does Luke know\n" .
             "that the GALACTIC EMPIRE\n" .
             "has secretly begun construction\n" .
             "on a new armored space station\n" .
             "even more powerful than the\n" .
             "first dreaded Death Star.\n" .
             "\n" .
             "When completed, this ultimate\n" .
             "weapon will spell certain\n" .
             "doom for the small band of\n" .
             "rebels struggling to restore\n" .
             "freedom to the galaxy...",
             0xf10dbeef0003);

function test_init($tarantool, $space_no) {
    try {
        global $sw4, $sw5, $sw6;
        $tarantool->insert($space_no, $sw4);
        $tarantool->insert($space_no, $sw5);
        $tarantool->insert($space_no, $sw6);
    } catch (Exception $e) {
        echo "init failed: ", $e->getMessage(), "\n";
        throw $e;
    }
}

function test_clean($tarantool, $space_no) {
    try {
        $tarantool->delete($space_no, 0);
        $tarantool->delete($space_no, 1);
        $tarantool->delete($space_no, 2);
    } catch (Exception $e) {
        echo "clean-up failed: ", $e->getMessage(), "\n";
        throw $e;
    }
}

function test_select($tarantool, $space_no, $index_no, $key) {
    try {
        $result = $tarantool->select($space_no, $index_no, $key);
        echo "result:\n";
        echo "count = ", $result["count"], "\n";
        $tuples_list = $result["tuples_list"];
        sort($tuples_list);
        for ($i = 0; $i < $result["count"]; $i++) {
            echo "tuple[", $i, "]:", "\n";
            echo "  id     = ", $tuples_list[$i][0], "\n";
            echo "  series = ", $tuples_list[$i][1], "\n";
            echo "  year   = ", $tuples_list[$i][2], "\n";
            echo "  name   = ", $tuples_list[$i][3], "\n";
            echo "  crawl  = ", $tuples_list[$i][4], "\n";
            echo "  uuid   = ", $tuples_list[$i][5], "\n";
        }
    } catch (Exception $e) {
        echo "catched exception: ", $e->getMessage(), "\n";
    }
}

function test_insert($tarantool, $space_no, $tuple, $flags) {
    try {
        $result = $tarantool->insert($space_no, $tuple, $flags);
        echo "result:\n";
        echo "count = ", $result["count"], "\n";
        if (array_key_exists("tuple", $result)) {
            echo "tuple:", "\n";
            echo "  id     = ", $result["tuple"][0], "\n";
            echo "  series = ", $result["tuple"][1], "\n";
            echo "  year   = ", $result["tuple"][2], "\n";
            echo "  name   = ", $result["tuple"][3], "\n";
            echo "  crawl  = ", $result["tuple"][4], "\n";
            echo "  uuid   = ", $result["tuple"][5], "\n";
        }
    } catch (Exception $e) {
        echo "catched exception: ", $e->getMessage(), "\n";
    }
}

function test_update_fields($tarantool, $space_no, $key, $ops, $flags) {
    try {
        $result = $tarantool->update_fields($space_no, $key, $ops, $flags);
        echo "result:\n";
        echo "count = ", $result["count"], "\n";
        if (array_key_exists("tuple", $result)) {
            echo "tuple:", "\n";
            echo "  id     = ", $result["tuple"][0], "\n";
            echo "  series = ", $result["tuple"][1], "\n";
            echo "  year   = ", $result["tuple"][2], "\n";
            echo "  name   = ", $result["tuple"][3], "\n";
            echo "  crawl  = ", $result["tuple"][4], "\n";
            echo "  uuid   = ", $result["tuple"][5], "\n";
        }
    } catch (Exception $e) {
        echo "catched exception: ", $e->getMessage(), "\n";
    }
}

function test_delete($tarantool, $space_no, $key, $flags) {
    try {
        $result = $tarantool->delete($space_no, $key, $flags);
        echo "result:\n";
        echo "count = ", $result["count"], "\n";
        if (array_key_exists("tuple", $result)) {
            echo "tuple:", "\n";
            echo "  id     = ", $result["tuple"][0], "\n";
            echo "  series = ", $result["tuple"][1], "\n";
            echo "  year   = ", $result["tuple"][2], "\n";
            echo "  name   = ", $result["tuple"][3], "\n";
            echo "  crawl  = ", $result["tuple"][4], "\n";
            echo "  uuid   = ", $result["tuple"][5], "\n";
        }
    } catch (Exception $e) {
        echo "catched exception: ", $e->getMessage(), "\n";
    }
}

function test_call($tarantool, $proc, $tuple_args, $flags) {
    try {
        $result = $tarantool->call($proc, $tuple_args, $flags);
        echo "result:\n";
        var_dump($result);
    } catch (Exception $e) {
        echo "catched exception: ", $e->getMessage(), "\n";
    }
}

?>
