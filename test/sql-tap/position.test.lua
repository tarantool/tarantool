#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(80)

test:do_test(
    "position-1.1",
    function()
        return test:execsql "SELECT position('a', 'abcdefg');"
    end, {
        -- <position-1.1>
        1
        -- </position-1.1>
    })

test:do_test(
    "position-1.2",
    function()
        return test:execsql "SELECT position('b', 'abcdefg');"
    end, {
        -- <position-1.2>
        2
        -- </position-1.2>
    })

test:do_test(
    "position-1.3",
    function()
        return test:execsql "SELECT position('c', 'abcdefg');"
    end, {
        -- <position-1.3>
        3
        -- </position-1.3>
    })

test:do_test(
    "position-1.4",
    function()
        return test:execsql "SELECT position('d', 'abcdefg');"
    end, {
        -- <position-1.4>
        4
        -- </position-1.4>
    })

test:do_test(
    "position-1.5",
    function()
        return test:execsql "SELECT position('e', 'abcdefg');"
    end, {
        -- <position-1.5>
        5
        -- </position-1.5>
    })

test:do_test(
    "position-1.6",
    function()
        return test:execsql "SELECT position('f', 'abcdefg');"
    end, {
        -- <position-1.6>
        6
        -- </position-1.6>
    })

test:do_test(
    "position-1.7",
    function()
        return test:execsql "SELECT position('g', 'abcdefg');"
    end, {
        -- <position-1.7>
        7
        -- </position-1.7>
    })

test:do_test(
    "position-1.8",
    function()
        return test:execsql "SELECT position('h', 'abcdefg');"
    end, {
        -- <position-1.8>
        0
        -- </position-1.8>
    })

test:do_test(
    "position-1.9",
    function()
        return test:execsql "SELECT position('abcdefg', 'abcdefg');"
    end, {
        -- <position-1.9>
        1
        -- </position-1.9>
    })

test:do_test(
    "position-1.10",
    function()
        return test:execsql "SELECT position('abcdefgh', 'abcdefg');"
    end, {
        -- <position-1.10>
        0
        -- </position-1.10>
    })

test:do_test(
    "position-1.11",
    function()
        return test:execsql "SELECT position('bcdefg', 'abcdefg');"
    end, {
        -- <position-1.11>
        2
        -- </position-1.11>
    })

test:do_test(
    "position-1.12",
    function()
        return test:execsql "SELECT position('bcdefgh', 'abcdefg');"
    end, {
        -- <position-1.12>
        0
        -- </position-1.12>
    })

test:do_test(
    "position-1.13",
    function()
        return test:execsql "SELECT position('cdefg', 'abcdefg');"
    end, {
        -- <position-1.13>
        3
        -- </position-1.13>
    })

test:do_test(
    "position-1.14",
    function()
        return test:execsql "SELECT position('cdefgh', 'abcdefg');"
    end, {
        -- <position-1.14>
        0
        -- </position-1.14>
    })

test:do_test(
    "position-1.15",
    function()
        return test:execsql "SELECT position('defg', 'abcdefg');"
    end, {
        -- <position-1.15>
        4
        -- </position-1.15>
    })

test:do_test(
    "position-1.16",
    function()
        return test:execsql "SELECT position('defgh', 'abcdefg');"
    end, {
        -- <position-1.16>
        0
        -- </position-1.16>
    })

test:do_test(
    "position-1.17",
    function()
        return test:execsql "SELECT position('efg', 'abcdefg');"
    end, {
        -- <position-1.17>
        5
        -- </position-1.17>
    })

test:do_test(
    "position-1.18",
    function()
        return test:execsql "SELECT position('efgh', 'abcdefg');"
    end, {
        -- <position-1.18>
        0
        -- </position-1.18>
    })

test:do_test(
    "position-1.19",
    function()
        return test:execsql "SELECT position('fg', 'abcdefg');"
    end, {
        -- <position-1.19>
        6
        -- </position-1.19>
    })

test:do_test(
    "position-1.20",
    function()
        return test:execsql "SELECT position('fgh', 'abcdefg');"
    end, {
        -- <position-1.20>
        0
        -- </position-1.20>
    })

test:do_test(
    "position-1.21",
    function()
        return test:execsql "SELECT coalesce(position(NULL, 'abcdefg'), 'nil');"
    end, {
        -- <position-1.21>
        "nil"
        -- </position-1.21>
    })

test:do_test(
    "position-1.22",
    function()
        return test:execsql "SELECT coalesce(position('x', NULL), 'nil');"
    end, {
        -- <position-1.22>
        "nil"
        -- </position-1.22>
    })

test:do_test(
    "position-1.23",
    function()
        return test:catchsql "SELECT position(34, 12345);"
    end, {
        -- <position-1.23>
        1, "Inconsistent types: expected string or varbinary got integer(12345)"
        -- </position-1.23>
    })

test:do_test(
    "position-1.24",
    function()
        return test:catchsql "SELECT position(34, 123456.78);"
    end, {
        -- <position-1.24>
        1, "Inconsistent types: expected string or varbinary got double(123456.78)"
        -- </position-1.24>
    })

test:do_test(
    "position-1.25",
    function()
        return test:catchsql "SELECT position(x'3334', 123456.78);"
    end, {
        -- <position-1.25>
        1, "Inconsistent types: expected string or varbinary got double(123456.78)"
        -- </position-1.25>
    })

test:do_test(
    "position-1.26",
    function()
        return test:execsql "SELECT position('efg', 'äbcdefg');"
    end, {
        -- <position-1.26>
        5
        -- </position-1.26>
    })

test:do_test(
    "position-1.27",
    function()
        return test:execsql "SELECT position('xyz', '€xyzzy');"
    end, {
        -- <position-1.27>
        2
        -- </position-1.27>
    })

test:do_test(
    "position-1.28",
    function()
        return test:execsql "SELECT position('xyz', 'abc€xyzzy');"
    end, {
        -- <position-1.28>
        5
        -- </position-1.28>
    })

test:do_test(
    "position-1.29",
    function()
        return test:execsql "SELECT position('€xyz', 'abc€xyzzy');"
    end, {
        -- <position-1.29>
        4
        -- </position-1.29>
    })

test:do_test(
    "position-1.30",
    function()
        return test:execsql "SELECT position('c€xyz', 'abc€xyzzy');"
    end, {
        -- <position-1.30>
        3
        -- </position-1.30>
    })

test:do_test(
    "position-1.31",
    function()
        return test:execsql "SELECT position(x'01', x'0102030405');"
    end, {
        -- <position-1.31>
        1
        -- </position-1.31>
    })

test:do_test(
    "position-1.32",
    function()
        return test:execsql "SELECT position(x'02', x'0102030405');"
    end, {
        -- <position-1.32>
        2
        -- </position-1.32>
    })

test:do_test(
    "position-1.33",
    function()
        return test:execsql "SELECT position(x'03', x'0102030405');"
    end, {
        -- <position-1.33>
        3
        -- </position-1.33>
    })

test:do_test(
    "position-1.34",
    function()
        return test:execsql "SELECT position(x'04', x'0102030405');"
    end, {
        -- <position-1.34>
        4
        -- </position-1.34>
    })

test:do_test(
    "position-1.35",
    function()
        return test:execsql "SELECT position(x'05', x'0102030405');"
    end, {
        -- <position-1.35>
        5
        -- </position-1.35>
    })

test:do_test(
    "position-1.36",
    function()
        return test:execsql "SELECT position(x'06', x'0102030405');"
    end, {
        -- <position-1.36>
        0
        -- </position-1.36>
    })

test:do_test(
    "position-1.37",
    function()
        return test:execsql "SELECT position(x'0102030405', x'0102030405');"
    end, {
        -- <position-1.37>
        1
        -- </position-1.37>
    })

test:do_test(
    "position-1.38",
    function()
        return test:execsql "SELECT position(x'02030405', x'0102030405');"
    end, {
        -- <position-1.38>
        2
        -- </position-1.38>
    })

test:do_test(
    "position-1.39",
    function()
        return test:execsql "SELECT position(x'030405', x'0102030405');"
    end, {
        -- <position-1.39>
        3
        -- </position-1.39>
    })

test:do_test(
    "position-1.40",
    function()
        return test:execsql "SELECT position(x'0405', x'0102030405');"
    end, {
        -- <position-1.40>
        4
        -- </position-1.40>
    })

test:do_test(
    "position-1.41",
    function()
        return test:execsql "SELECT position(x'0506', x'0102030405');"
    end, {
        -- <position-1.41>
        0
        -- </position-1.41>
    })

test:do_test(
    "position-1.42",
    function()
        return test:execsql "SELECT position(x'', x'0102030405');"
    end, {
        -- <position-1.42>
        1
        -- </position-1.42>
    })

test:do_test(
    "position-1.43",
    function()
        return test:execsql "SELECT position(x'', x'');"
    end, {
        -- <position-1.43>
        1
        -- </position-1.43>
    })

test:do_test(
    "position-1.44",
    function()
        return test:execsql "SELECT position('', '');"
    end, {
        -- <position-1.44>
        1
        -- </position-1.44>
    })

test:do_test(
    "position-1.45",
    function()
        return test:execsql "SELECT position('', 'abcdefg');"
    end, {
        -- <position-1.45>
        1
        -- </position-1.45>
    })

local longstr = "abcdefghijklmonpqrstuvwxyz"
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
longstr = longstr .. longstr
-- puts [string length '"..longstr.."']
longstr = longstr .. "Xabcde"
test:do_test(
    "position-1.46",
    function()
        return test:execsql("SELECT position('X', '"..longstr.."');")
    end, {
        -- <position-1.46>
        106497
        -- </position-1.46>
    })

test:do_test(
    "position-1.47",
    function()
        return test:execsql("SELECT position('Y', '"..longstr.."');")
    end, {
        -- <position-1.47>
        0
        -- </position-1.47>
    })

test:do_test(
    "position-1.48",
    function()
        return test:execsql( "SELECT position('Xa', '"..longstr.."');")
    end, {
        -- <position-1.48>
        106497
        -- </position-1.48>
    })

test:do_test(
    "position-1.49",
    function()
        return test:execsql("SELECT position('zXa', '"..longstr.."');")
    end, {
        -- <position-1.49>
        106496
        -- </position-1.49>
    })

longstr = string.gsub(longstr, "a", "ä")
test:do_test(
    "position-1.50",
    function()
        return test:execsql("SELECT position('X', '"..longstr.."');")
    end, {
        -- <position-1.50>
        106497
        -- </position-1.50>
    })

test:do_test(
    "position-1.51",
    function()
        return test:execsql("SELECT position('Y', '"..longstr.."');")
    end, {
        -- <position-1.51>
        0
        -- </position-1.51>
    })

test:do_test(
    "position-1.52",
    function()
        return test:execsql("SELECT position('Xä', '"..longstr.."');")
    end, {
        -- <position-1.52>
        106497
        -- </position-1.52>
    })

test:do_test(
    "position-1.53",
    function()
        return test:execsql("SELECT position('zXä', '"..longstr.."');")
    end, {
        -- <position-1.53>
        106496
        -- </position-1.53>
    })

test:do_test(
    "position-1.54",
    function()
        return test:catchsql("SELECT position('x', x'78c3a4e282ac79');")
    end, {
        -- <position-1.54>
        1, "Inconsistent types: expected string got varbinary(x'78C3A4E282AC79')"
        -- </position-1.54>
    })

test:do_test(
    "position-1.55",
    function()
        return test:catchsql "SELECT position('y', x'78c3a4e282ac79');"
    end, {
        -- <position-1.55>
        1, "Inconsistent types: expected string got varbinary(x'78C3A4E282AC79')"
        -- </position-1.55>
    })

test:do_test(
    "position-1.56.1",
    function()
        return test:execsql "SELECT position(x'79', x'78c3a4e282ac79');"
    end, {
        -- <position-1.56.1>
        7
        -- </position-1.56.1>
    })

test:do_test(
    "position-1.56.2",
    function()
        return test:execsql "SELECT position(x'7a', x'78c3a4e282ac79');"
    end, {
        -- <position-1.56.2>
        0
        -- </position-1.56.2>
    })

test:do_test(
    "position-1.56.3",
    function()
        return test:execsql "SELECT position(x'78', x'78c3a4e282ac79');"
    end, {
        -- <position-1.56.3>
        1
        -- </position-1.56.3>
    })

test:do_test(
    "position-1.56.3",
    function()
        return test:execsql "SELECT position(x'a4', x'78c3a4e282ac79');"
    end, {
        -- <position-1.56.3>
        3
        -- </position-1.56.3>
    })

test:do_test(
    "position-1.57.1",
    function()
        return test:catchsql "SELECT position(x'79', 'xä€y');"
    end, {
        -- <position-1.57.1>
        1, "Inconsistent types: expected varbinary got string('xä€y')"
        -- </position-1.57.1>
    })

test:do_test(
    "position-1.57.2",
    function()
        return test:catchsql "SELECT position(x'a4', 'xä€y');"
    end, {
        -- <position-1.57.2>
        1, "Inconsistent types: expected varbinary got string('xä€y')"
        -- </position-1.57.2>
    })

test:do_test(
    "position-1.57.3",
    function()
        return test:catchsql "SELECT position('y', x'78c3a4e282ac79');"
    end, {
        -- <position-1.57.3>
        1, "Inconsistent types: expected string got varbinary(x'78C3A4E282AC79')"
        -- </position-1.57.3>
    })

-- If either X or Y are NULL in position(X,Y)
-- then the result is NULL.
--
test:do_execsql_test(
    "position-1.60",
    [[
        SELECT coalesce(position(NULL, 'abc'), 999);
    ]], {
        -- <position-1.60>
        999
        -- </position-1.60>
    })

test:do_execsql_test(
    "position-1.61",
    [[
        SELECT coalesce(position('abc', NULL), 999);
    ]], {
        -- <position-1.61>
        999
        -- </position-1.61>
    })

test:do_execsql_test(
    "position-1.62",
    [[
        SELECT coalesce(position(NULL, NULL), 999);
    ]], {
        -- <position-1.62>
        999
        -- </position-1.62>
    })

-- Tests that make use of collations.
-- A short remainder of how "unicode" and "unicode_ci" collations
-- works:
-- unicode_ci: „a“ = „A“ = „á“ = „Á“.
-- unicode: all symbols above are pairwise unequal.

-- Collation is set in space format.

test:do_execsql_test(
    "position-1.63",
    [[
        CREATE TABLE test1 (s1 VARCHAR(5) PRIMARY KEY COLLATE "unicode_ci");
        INSERT INTO test1 VALUES('à');
        SELECT POSITION('a', s1) FROM test1;
        DELETE FROM test1;
    ]], {
        1
    }
)

test:do_execsql_test(
    "position-1.64",
    [[
        INSERT INTO test1 VALUES('qwèrty');
        SELECT POSITION('er', s1) FROM test1;
        DELETE FROM test1;
    ]], {
        3
    }
)

test:do_execsql_test(
    "position-1.65",
    [[
        INSERT INTO test1 VALUES('qwèrtÿ');
        SELECT POSITION('y', s1) FROM test1;
        DELETE FROM test1;
    ]], {
        6
    }
)

-- Collation is set in space format and also in position() -
-- for haystack (string) only.

test:do_execsql_test(
    "position-1.66",
    [[
        INSERT INTO test1 VALUES('à');
        SELECT POSITION('a', s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.67",
    [[
        INSERT INTO test1 VALUES('qwèrty');
        SELECT POSITION('er', s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.68",
    [[
        INSERT INTO test1 VALUES('qwèrtÿ');
        SELECT POSITION('Y', s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

-- Collation is set in space format and also in position () -
-- for needle (string) only.

test:do_execsql_test(
    "position-1.69",
    [[
        INSERT INTO test1 VALUES('à');
        SELECT POSITION('a' COLLATE "unicode", s1) FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.70",
    [[
        INSERT INTO test1 VALUES('qwèrty');
        SELECT POSITION('er' COLLATE "unicode", s1) FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.71",
    [[
        INSERT INTO test1 VALUES('qwèrtÿ');
        SELECT POSITION('Y' COLLATE "unicode", s1) FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

-- Collation is set in space format and also in position() -
-- for both arguments. Arguments have the same collations.

test:do_execsql_test(
    "position-1.72",
    [[
        INSERT INTO test1 VALUES('à');
        SELECT POSITION('a' COLLATE "unicode", s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.73",
    [[
        INSERT INTO test1 VALUES('qwèrty');
        SELECT POSITION('er' COLLATE "unicode", s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

test:do_execsql_test(
    "position-1.74",
    [[
        INSERT INTO test1 VALUES('qwèrtÿ');
        SELECT POSITION('Y'COLLATE "unicode", s1 COLLATE "unicode") FROM test1;
        DELETE FROM test1;
    ]], {
        0
    }
)

-- Collation is set in space format and also in position() -
-- for both arguments. Arguments have different explicit
-- collations thus an error is expected.

test:do_catchsql_test(
    "position-1.75",
    [[
        DELETE FROM test1;
        INSERT INTO test1 VALUES('à');
        SELECT POSITION('a' COLLATE "unicode_ci", s1 COLLATE "unicode") FROM test1;
    ]], {
        1, "Illegal mix of collations"
    }
)

test:do_catchsql_test(
    "position-1.76",
    [[
        DELETE FROM test1;
        INSERT INTO test1 VALUES('qwèrty');
        SELECT POSITION('er' COLLATE "unicode_ci", s1 COLLATE "unicode") FROM test1;
    ]], {
        1, "Illegal mix of collations"
    }
)

test:do_catchsql_test(
    "position-1.77",
    [[
        DELETE FROM test1;
        INSERT INTO test1 VALUES('qwèrtÿ');
        SELECT POSITION('Y' COLLATE "unicode_ci", s1 COLLATE "unicode") FROM test1;
    ]], {
        1, "Illegal mix of collations"
    }
)

test:finish_test()
