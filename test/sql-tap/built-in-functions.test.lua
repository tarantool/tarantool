#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(66)

--
-- Make sure that number of arguments check is checked properly for SQL built-in
-- functions with variable number of arguments.
--
test:do_catchsql_test(
    "builtins-1.1",
    [[
        SELECT COUNT(1, 2);
    ]],
    {
        1, [[Wrong number of arguments is passed to COUNT(): ]]..
           [[expected from 0 to 1, got 2]]
    }
)

test:do_catchsql_test(
    "builtins-1.2",
    [[
        SELECT GREATEST();
    ]],
    {
        1, [[Wrong number of arguments is passed to GREATEST(): ]]..
           [[expected at least 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.3",
    [[
        SELECT GROUP_CONCAT();
    ]],
    {
        1, [[Wrong number of arguments is passed to GROUP_CONCAT(): ]]..
           [[expected from 1 to 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.4",
    [[
        SELECT GROUP_CONCAT(1, 2, 3);
    ]],
    {
        1, [[Wrong number of arguments is passed to GROUP_CONCAT(): ]]..
           [[expected from 1 to 2, got 3]]
    }
)

test:do_catchsql_test(
    "builtins-1.5",
    [[
        SELECT LEAST();
    ]],
    {
        1, [[Wrong number of arguments is passed to LEAST(): ]]..
           [[expected at least 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.6",
    [[
        SELECT ROUND();
    ]],
    {
        1, [[Wrong number of arguments is passed to ROUND(): ]]..
           [[expected from 1 to 2, got 0]]
    }
)

test:do_catchsql_test(
    "builtins-1.7",
    [[
        SELECT ROUND(1, 2, 3);
    ]],
    {
        1, [[Wrong number of arguments is passed to ROUND(): ]]..
           [[expected from 1 to 2, got 3]]
    }
)

test:do_catchsql_test(
    "builtins-1.8",
    [[
        SELECT SUBSTR('1');
    ]],
    {
        1, [[Wrong number of arguments is passed to SUBSTR(): ]]..
           [[expected from 2 to 3, got 1]]
    }
)

test:do_catchsql_test(
    "builtins-1.9",
    [[
        SELECT SUBSTR('1', '2', '3', '4');
    ]],
    {
        1, [[Wrong number of arguments is passed to SUBSTR(): ]]..
           [[expected from 2 to 3, got 4]]
    }
)

test:do_catchsql_test(
    "builtins-1.10",
    [[
        SELECT UUID(1, 2);
    ]],
    {
        1, [[Wrong number of arguments is passed to UUID(): ]]..
           [[expected from 0 to 1, got 2]]
    }
)

-- Make sure static and dynamic argument type checking is working correctly.

test:do_catchsql_test(
    "builtins-2.1",
    [[
        SELECT CHAR_LENGTH(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function CHAR_LENGTH()]]
    }
)

test:do_test(
    "builtins-2.2",
    function()
        local res = {pcall(box.execute, [[SELECT CHAR_LENGTH(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.3",
    [[
        SELECT CHARACTER_LENGTH(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function CHARACTER_LENGTH()]]
    }
)

test:do_test(
    "builtins-2.4",
    function()
        local res = {pcall(box.execute, [[SELECT CHARACTER_LENGTH(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.5",
    [[
        SELECT CHAR('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function CHAR()]]
    }
)

test:do_test(
    "builtins-2.6",
    function()
        local res = {pcall(box.execute, [[SELECT CHAR(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to integer"
    })

test:do_catchsql_test(
    "builtins-2.7",
    [[
        SELECT HEX(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function HEX()]]
    }
)

test:do_test(
    "builtins-2.8",
    function()
        local res = {pcall(box.execute, [[SELECT HEX(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to varbinary"
    })

test:do_catchsql_test(
    "builtins-2.9",
    [[
        SELECT LENGTH(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function LENGTH()]]
    }
)

test:do_test(
    "builtins-2.10",
    function()
        local res = {pcall(box.execute, [[SELECT LENGTH(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.11",
    [[
        SELECT 1 LIKE '%';
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function LIKE()]]
    }
)

test:do_test(
    "builtins-2.12",
    function()
        local res = {pcall(box.execute, [[SELECT ? LIKE '%';]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.13",
    [[
        SELECT LOWER(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function LOWER()]]
    }
)

test:do_test(
    "builtins-2.14",
    function()
        local res = {pcall(box.execute, [[SELECT LOWER(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.15",
    [[
        SELECT UPPER(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function UPPER()]]
    }
)

test:do_test(
    "builtins-2.16",
    function()
        local res = {pcall(box.execute, [[SELECT UPPER(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.17",
    [[
        SELECT POSITION(1, 1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function POSITION()]]
    }
)

test:do_test(
    "builtins-2.18",
    function()
        local res = {pcall(box.execute, [[SELECT POSITION(?, ?);]], {1, 1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.19",
    [[
        SELECT RANDOMBLOB('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function RANDOMBLOB()]]
    }
)

test:do_test(
    "builtins-2.20",
    function()
        local res = {pcall(box.execute, [[SELECT RANDOMBLOB(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to integer"
    })

test:do_catchsql_test(
    "builtins-2.21",
    [[
        SELECT ZEROBLOB('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function ZEROBLOB()]]
    }
)

test:do_test(
    "builtins-2.22",
    function()
        local res = {pcall(box.execute, [[SELECT ZEROBLOB(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to integer"
    })

test:do_catchsql_test(
    "builtins-2.23",
    [[
        SELECT SOUNDEX(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function SOUNDEX()]]
    }
)

test:do_test(
    "builtins-2.24",
    function()
        local res = {pcall(box.execute, [[SELECT SOUNDEX(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.25",
    [[
        SELECT UNICODE(1);
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function UNICODE()]]
    }
)

test:do_test(
    "builtins-2.26",
    function()
        local res = {pcall(box.execute, [[SELECT UNICODE(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_catchsql_test(
    "builtins-2.27",
    [[
        SELECT ABS('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function ABS()]]
    }
)

test:do_test(
    "builtins-2.28",
    function()
        local res = {pcall(box.execute, [[SELECT ABS(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to decimal"
    })

test:do_catchsql_test(
    "builtins-2.29",
    [[
        SELECT ROUND('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function ROUND()]]
    }
)

test:do_test(
    "builtins-2.30",
    function()
        local res = {pcall(box.execute, [[SELECT ROUND(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to decimal"
    })

test:do_catchsql_test(
    "builtins-2.31",
    [[
        SELECT UUID('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function UUID()]]
    }
)

test:do_test(
    "builtins-2.32",
    function()
        local res = {pcall(box.execute, [[SELECT UUID(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to integer"
    })

test:do_catchsql_test(
    "builtins-2.33",
    [[
        SELECT SUM('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function SUM()]]
    }
)

test:do_test(
    "builtins-2.34",
    function()
        local res = {pcall(box.execute, [[SELECT SUM(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to decimal"
    })

test:do_catchsql_test(
    "builtins-2.35",
    [[
        SELECT AVG('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function AVG()]]
    }
)

test:do_test(
    "builtins-2.36",
    function()
        local res = {pcall(box.execute, [[SELECT AVG(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to decimal"
    })

test:do_catchsql_test(
    "builtins-2.37",
    [[
        SELECT TOTAL('1');
    ]],
    {
        1, [[Failed to execute SQL statement: ]]..
           [[wrong arguments for function TOTAL()]]
    }
)

test:do_test(
    "builtins-2.38",
    function()
        local res = {pcall(box.execute, [[SELECT TOTAL(?);]], {'1'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('1') to decimal"
    })

--
-- Make sure that the type of result of MAX() and MIN() is the same as the type
-- of the argument.
--
test:do_execsql_test(
    "builtins-3.1",
    [[
        SELECT TYPEOF(1), TYPEOF(MAX(1)), TYPEOF(MIN(1));
    ]],
    {
        'integer', 'integer', 'integer'
    }
)

test:do_test(
    "builtins-3.2",
    function()
        return box.execute([[SELECT 1, MAX(1), MIN(1);]]).metadata
    end, {
        {name = "COLUMN_1", type = "integer"},
        {name = "COLUMN_2", type = "integer"},
        {name = "COLUMN_3", type = "integer"}
    })

--
-- Make sure that the type of result of GREATEST() and LEAST() depends on type
-- of arguments.
--
test:do_execsql_test(
    "builtins-3.3",
    [[
        SELECT TYPEOF(GREATEST('1', 1)), TYPEOF(LEAST('1', 1));
    ]],
    {
        'scalar', 'scalar'
    }
)

test:do_test(
    "builtins-3.4",
    function()
        return box.execute([[SELECT GREATEST('1', 1), LEAST('1', 1);]]).metadata
    end, {
        {name = "COLUMN_1", type = "scalar"},
        {name = "COLUMN_2", type = "scalar"},
    })

-- gh-6483: Make sure functions have correct default type.
test:do_test(
    "builtins-4.1",
    function()
        return box.execute([[SELECT ABS(?);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'decimal'
    })

test:do_test(
    "builtins-4.2",
    function()
        return box.execute([[SELECT AVG(?);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'decimal'
    })

test:do_test(
    "builtins-4.3",
    function()
        return box.execute([[SELECT GREATEST(?, 1);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'scalar'
    })

test:do_test(
    "builtins-4.4",
    function()
        return box.execute([[SELECT GROUP_CONCAT(?);]], {'a'}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'string'
    })

test:do_test(
    "builtins-4.5",
    function()
        return box.execute([[SELECT LEAST(?, 1);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'scalar'
    })

test:do_test(
    "builtins-4.6",
    function()
        local res = {pcall(box.execute, [[SELECT LENGTH(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_test(
    "builtins-4.7",
    function()
        return box.execute([[SELECT MAX(?);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'scalar'
    })

test:do_test(
    "builtins-4.8",
    function()
        return box.execute([[SELECT MIN(?);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'scalar'
    })

test:do_test(
    "builtins-4.9",
    function()
        local res = {pcall(box.execute, [[SELECT POSITION(?, ?);]], {1, 1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:do_test(
    "builtins-4.10",
    function()
        return box.execute([[SELECT REPLACE(@1, @1, @1);]], {'a'}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'string'
    })

test:do_test(
    "builtins-4.11",
    function()
        return box.execute([[SELECT SUBSTR(?, 1, 2);]], {'asd'}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'string'
    })

test:do_test(
    "builtins-4.12",
    function()
        return box.execute([[SELECT SUM(?);]], {1}).metadata[1]
    end, {
        name = "COLUMN_1", type = 'decimal'
    })

test:do_test(
    "builtins-4.13",
    function()
        local res = {pcall(box.execute, [[SELECT TOTAL(?);]], {'a'})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert string('a') to decimal"
    })

test:do_test(
    "builtins-4.14",
    function()
        local res = {pcall(box.execute, [[SELECT TRIM(?);]], {1})}
        return {tostring(res[3])}
    end, {
        "Type mismatch: can not convert integer(1) to string"
    })

test:finish_test()
