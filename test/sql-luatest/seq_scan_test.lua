local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'seq_scan'})
    g.server:start()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, a INT);]])
        box.execute([[INSERT INTO t VALUES (1, 1), (2, 2), (3, 3);]])
        box.execute([[CREATE TABLE s (i INT PRIMARY KEY, a INT);]])
        box.execute([[INSERT INTO s VALUES (1, 1), (2, 2), (3, 3);]])
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.execute([[DROP TABLE t;]])
        box.execute([[DROP TABLE s;]])
    end)
    g.server:stop()
end)

g.test_seq_scan_error = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])

        local _, err = box.execute([[SELECT * FROM t;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        _, err = box.execute([[SELECT * FROM t WHERE i + 1 > 2;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        _, err = box.execute([[SELECT * FROM t WHERE a > 2;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")
    end)
end

g.test_seq_scan_success = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])

        local res, err = box.execute([[SELECT max(i) FROM t;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT min(i) FROM t;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t WHERE i = 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t WHERE i < 2;]])
        t.assert(res ~= nil and err == nil)
    end)
end

g.test_seq_scan_keyword = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])

        local res, err = box.execute([[SELECT * FROM SEQSCAN t;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM SEQSCAN t WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM SEQSCAN t WHERE i + 1 > 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM SEQSCAN t WHERE a > 2;]])
        t.assert(res ~= nil and err == nil)
    end)
end

g.test_seq_scan_joins = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])

        local _, err = box.execute([[SELECT * FROM t, s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM SEQSCAN t, s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM t, SEQSCAN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        local res
        res, err = box.execute([[SELECT * FROM SEQSCAN t, SEQSCAN s;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t, s USING(i);]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t, s USING(i);]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t, s USING(i) WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM SEQSCAN t JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM t JOIN SEQSCAN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t JOIN SEQSCAN s;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t JOIN s USING(i);]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t JOIN s USING(i);]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t JOIN s USING(i) WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t LEFT JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM SEQSCAN t LEFT JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM t LEFT JOIN SEQSCAN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t LEFT JOIN SEQSCAN s;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t LEFT JOIN s USING(i);]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t
                                 LEFT JOIN s USING(i);]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t
                                 LEFT JOIN s USING(i) WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t INNER JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM SEQSCAN t INNER JOIN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'S'")

        _, err = box.execute([[SELECT * FROM t INNER JOIN SEQSCAN s;]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t INNER JOIN
                                 SEQSCAN s;]])
        t.assert(res ~= nil and err == nil)

        _, err = box.execute([[SELECT * FROM t INNER JOIN s USING(i);]])
        t.assert_equals(err.message, "Scanning is not allowed for 'T'")

        res, err = box.execute([[SELECT * FROM SEQSCAN t
                                 INNER JOIN s USING(i);]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t
                                 INNER JOIN s USING(i) WHERE i > 2;]])
        t.assert(res ~= nil and err == nil)
    end)
end

g.test_seq_scan_enabled = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])

        local res, err = box.execute([[SELECT * FROM t;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM SEQSCAN t;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t WHERE i + 1 > 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t WHERE a > 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t, s;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM SEQSCAN t, s;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[SELECT * FROM t, SEQSCAN s;]])
        t.assert(res ~= nil and err == nil)
    end)
end

g.test_seq_scan_not_select = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])

        local res, err = box.execute([[UPDATE t SET a = 10 WHERE i + 1 = 2;]])
        t.assert(res ~= nil and err == nil)

        res, err = box.execute([[DELETE FROM t WHERE i + 1 = 2;]])
        t.assert(res ~= nil and err == nil)
    end)
end
