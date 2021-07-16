-- Create table for tests
CREATE TABLE t (a BOOLEAN PRIMARY KEY);
INSERT INTO t VALUES (true), (false);

-- Create user-defined function.
\set language lua
test_run = require('test_run').new()
test_run:cmd("setopt delimiter ';'")
box.schema.func.create('RETURN_TYPE', {language = 'Lua',
		       is_deterministic = true,
		       body = 'function(x) return type(x) end',
		       returns = 'string', param_list = {'any'},
		       exports = {'LUA', 'SQL'}});
box.schema.func.create('IS_BOOLEAN', {language = 'Lua',
		       is_deterministic = true,
		       body = "function(x) return type(x) == 'boolean' end",
		       returns = 'boolean', param_list = {'any'},
		       exports = {'LUA', 'SQL'}});
test_run:cmd("setopt delimiter ''");
\set language sql

-- Check boolean as WHERE argument.
SELECT a FROM t WHERE a;
SELECT a FROM t WHERE a != true;

-- Check DEFAULT values for boolean.
CREATE TABLE t0 (i INT PRIMARY KEY, a BOOLEAN DEFAULT true);
INSERT INTO t0 VALUES (1, false);
INSERT INTO t0(i) VALUES (2);
INSERT INTO t0 VALUES (3, NULL);
SELECT * FROM t0;

-- Check UNKNOWN value for boolean.
INSERT INTO t0 VALUES (4, UNKNOWN);
SELECT * FROM t0;

-- Make sure that SCALAR can handle boolean values.
CREATE TABLE ts (id INT PRIMARY KEY AUTOINCREMENT, s SCALAR);
INSERT INTO ts SELECT * FROM t0;
SELECT s FROM ts WHERE s = true;
INSERT INTO ts(s) VALUES ('abc'), (12.5);
SELECT s FROM ts WHERE s = true;
SELECT s FROM ts WHERE s < true;
SELECT s FROM ts WHERE s IN (true, 1, 'abcd');

--
-- Make sure that BOOLEAN is not implicitly converted to INTEGER
-- while inserted to PRIMARY KEY field.
--
INSERT INTO ts VALUES (true, 12345);

-- Check that we can create index on field of type BOOLEAN.
CREATE INDEX i0 ON t0(a);

-- Check boolean as LIMIT argument.
SELECT * FROM t LIMIT true;
SELECT * FROM t LIMIT false;

-- Check boolean as OFFSET argument.
SELECT * FROM t LIMIT 1 OFFSET true;
SELECT * FROM t LIMIT 1 OFFSET false;

-- Check involvance in index search.
EXPLAIN QUERY PLAN SELECT a FROM t0 WHERE a = true;

-- Check that ephemeral tables are used with BOOLEAN.
\set language lua
result = box.execute('EXPLAIN SELECT * FROM (VALUES(true)), t;')
i = 0
for _,v in pairs(result.rows) do if (v[2] == 'OpenTEphemeral') then i = i + 1 end end
i > 0
\set language sql

-- Check BOOLEAN as argument of user-defined function.
SELECT return_type(a) FROM t;
SELECT return_type('false');
SELECT is_boolean(a) FROM t LIMIT 1;
SELECT is_boolean('true');

-- Check BOOLEAN as argument of scalar function.
SELECT abs(a) FROM t0;
SELECT lower(a) FROM t0;
SELECT upper(a) FROM t0;
SELECT quote(a) FROM t0;
-- gh-4462: LENGTH didn't take BOOLEAN arguments.
SELECT length(a) FROM t0;
SELECT typeof(a) FROM t0;

-- Check BOOLEAN as argument of aggregate function.
SELECT AVG(a) FROM t0;
SELECT MIN(a) FROM t0;
SELECT MAX(a) FROM t0;
SELECT SUM(a) FROM t0;
SELECT COUNT(a) FROM t0;
SELECT TOTAL(a) FROM t0;
SELECT GROUP_CONCAT(a, ' +++ ') FROM t0;

-- Check BOOLEAN as binding parameter.
\set language lua
box.execute('SELECT ?, ?, return_type($1), typeof($2);', {true, false})

parameters = {}
parameters[1] = {}
parameters[1]['@value2'] = true
parameters[2] = {}
parameters[2][':value1'] = false
box.execute('SELECT :value1, @value2;', parameters)
\set language sql

-- Check interactions with CHECK constraint.
CREATE TABLE t1 (i INT PRIMARY KEY, a BOOLEAN, CONSTRAINT ck CHECK(a != true));
INSERT INTO t1 VALUES (1, false);
INSERT INTO t1 VALUES (2, true);

-- Check interactions with FOREIGN KEY constraint.
CREATE TABLE t2 (a BOOLEAN PRIMARY KEY, b BOOLEAN REFERENCES t2(a));
INSERT INTO t2 VALUES (false, true)
INSERT INTO t2 VALUES (true, false)
INSERT INTO t2 VALUES (true, true)
INSERT INTO t2 VALUES (false, true)

-- Check interactions with UNIQUE constraint.
CREATE TABLE t3 (i INT PRIMARY KEY, a BOOLEAN, CONSTRAINT uq UNIQUE(a));
INSERT INTO t3 VALUES (1, true)
INSERT INTO t3 VALUES (2, false)
INSERT INTO t3 VALUES (3, true)
INSERT INTO t3 VALUES (4, false)

-- Check CAST from BOOLEAN to the other types.
SELECT cast(true AS INTEGER);
SELECT cast(false AS INTEGER);
SELECT cast(true AS NUMBER);
SELECT cast(false AS NUMBER);
-- gh-4462: ensure that text representation is uppercase.
SELECT cast(true AS TEXT), cast(false AS TEXT);
SELECT cast(true AS BOOLEAN), cast(false AS BOOLEAN);

-- Check CAST to BOOLEAN from the other types.
SELECT cast(100 AS BOOLEAN);
SELECT cast(1 AS BOOLEAN);
SELECT cast(0 AS BOOLEAN);
SELECT cast(0.123 AS BOOLEAN);
SELECT cast(0.0 AS BOOLEAN);
SELECT cast('true' AS BOOLEAN), cast('false' AS BOOLEAN);
SELECT cast('TRUE' AS BOOLEAN), cast('FALSE' AS BOOLEAN);

-- Check usage in trigger.
CREATE TABLE t4 (i INT PRIMARY KEY, a BOOLEAN);
CREATE TABLE t5 (i INT PRIMARY KEY AUTOINCREMENT, b BOOLEAN);
CREATE TRIGGER r AFTER INSERT ON t4 FOR EACH ROW BEGIN INSERT INTO t5(b) VALUES(true); END;
INSERT INTO t4 VALUES (100, false);
DROP TRIGGER r;
SELECT * FROM t4;
SELECT * FROM t5;

-- Check UNION, UNION ALL AND INTERSECT.
INSERT INTO t5 VALUES (100, false);
SELECT * FROM t4 UNION SELECT * FROM t5;
SELECT * FROM t4 UNION ALL SELECT * FROM t5;
SELECT * FROM t4 INTERSECT SELECT * FROM t5;

-- Check SUBSELECT.
INSERT INTO t5(b) SELECT a FROM t4;
SELECT * FROM t5;

SELECT * FROM (SELECT t4.i, t5.i, a, b FROM t4, t5 WHERE a = false OR b = true);

-- Check VIEW.
CREATE VIEW v AS SELECT b FROM t5;
SELECT * FROM v;

-- Check DISTINCT.
SELECT DISTINCT * FROM v;

-- Check CTE
WITH temp(x, y) AS (VALUES (111, false) UNION ALL VALUES (222, true)) \
INSERT INTO t4 SELECT * from temp;
SELECT * FROM t4;

WITH RECURSIVE cnt(x, y) AS \
(VALUES(1, false) UNION ALL SELECT x+1, x % 2 == 1 FROM cnt WHERE x<10) \
SELECT x, y FROM cnt;

-- Check JOINs.
SELECT * FROM t4 JOIN t5 ON t4.a = t5.b;
SELECT * FROM t4 LEFT JOIN t5 ON t4.a = t5.b;
SELECT * FROM t4 INNER JOIN t5 ON t4.a = t5.b;

-- Check UPDATE.
UPDATE t4 SET a = NOT a;
SELECT * FROM t4;

-- Check SWITCH-CASE.
SELECT i, \
CASE \
	WHEN a == true AND i % 2 == 1 THEN false \
	WHEN a == true and i % 2 == 0 THEN true \
	WHEN a != true then false \
END AS a0 \
FROM t4;

-- Check ORDER BY.
SELECT * FROM t4 UNION SELECT * FROM t5 ORDER BY a, i;

-- Check GROUP BY.
SELECT a, COUNT(*) FROM (SELECT * FROM t4 UNION SELECT * FROM t5) GROUP BY a;

-- Check logical, bitwise and arithmetical operations.
CREATE TABLE t6 (a1 BOOLEAN PRIMARY KEY, a2 BOOLEAN);
INSERT INTO t6 VALUES (true, false), (false, true);

SELECT NOT true;
SELECT NOT false;
SELECT a, NOT a FROM t;

SELECT true AND true;
SELECT true AND false;
SELECT false AND true;
SELECT false AND false;
SELECT true OR true;
SELECT true OR false;
SELECT false OR true;
SELECT false OR false;

SELECT a, true AND a FROM t;
SELECT a, false AND a FROM t;
SELECT a, true OR a FROM t;
SELECT a, false OR a FROM t;
SELECT a, a AND true FROM t;
SELECT a, a AND false FROM t;
SELECT a, a OR true FROM t;
SELECT a, a OR false FROM t;

SELECT a, a1, a AND a1 FROM t, t6;
SELECT a, a1, a OR a1 FROM t, t6;

SELECT -true;
SELECT -false;
SELECT -a FROM t;

SELECT true + true;
SELECT true + false;
SELECT false + true;
SELECT false + false;
SELECT true - true;
SELECT true - false;
SELECT false - true;
SELECT false - false;
SELECT true * true;
SELECT true * false;
SELECT false * true;
SELECT false * false;
SELECT true / true;
SELECT true / false;
SELECT false / true;
SELECT false / false;
SELECT true % true;
SELECT true % false;
SELECT false % true;
SELECT false % false;

SELECT a, true + a FROM t;
SELECT a, false + a FROM t;
SELECT a, true - a FROM t;
SELECT a, false - a FROM t;
SELECT a, true * a FROM t;
SELECT a, false * a FROM t;
SELECT a, true / a FROM t;
SELECT a, false / a FROM t;
SELECT a, true % a FROM t;
SELECT a, false % a FROM t;
SELECT a, a + true FROM t;
SELECT a, a + false FROM t;
SELECT a, a - true FROM t;
SELECT a, a - false FROM t;
SELECT a, a * true FROM t;
SELECT a, a * false FROM t;
SELECT a, a / true FROM t;
SELECT a, a / false FROM t;
SELECT a, a % true FROM t;
SELECT a, a % false FROM t;

SELECT a, a1, a + a1 FROM t, t6;
SELECT a, a1, a - a1 FROM t, t6;
SELECT a, a1, a * a1 FROM t, t6;
SELECT a, a1, a / a1 FROM t, t6;
SELECT a, a1, a % a1 FROM t, t6;

SELECT ~true;
SELECT ~false;
SELECT true & true;
SELECT true & false;
SELECT false & true;
SELECT false & false;
SELECT true | true;
SELECT true | false;
SELECT false | true;
SELECT false | false;
SELECT true << true;
SELECT true << false;
SELECT false << true;
SELECT false << false;
SELECT true >> true;
SELECT true >> false;
SELECT false >> true;
SELECT false >> false;

SELECT a, true & a FROM t;
SELECT a, false & a FROM t;
SELECT a, true | a FROM t;
SELECT a, false | a FROM t;
SELECT a, true << a FROM t;
SELECT a, false << a FROM t;
SELECT a, true >> a FROM t;
SELECT a, false >> a FROM t;
SELECT a, a & true FROM t;
SELECT a, a & false FROM t;
SELECT a, a | true FROM t;
SELECT a, a | false FROM t;
SELECT a, a << true FROM t;
SELECT a, a << false FROM t;
SELECT a, a >> true FROM t;
SELECT a, a >> false FROM t;

SELECT a, a1, a & a1 FROM t, t6;
SELECT a, a1, a | a1 FROM t, t6;
SELECT a, a1, a << a1 FROM t, t6;
SELECT a, a1, a >> a1 FROM t, t6;

-- Check concatenate.
SELECT true || true;
SELECT true || false;
SELECT false || true;
SELECT false || false;

SELECT a, true || a FROM t;
SELECT a, false || a FROM t;
SELECT a, a || true FROM t;
SELECT a, a || false FROM t;

SELECT a1, a1 || a1 FROM t6;
SELECT a2, a2 || a2 FROM t6;
SELECT a1, a2, a1 || a1 FROM t6;
SELECT a1, a2, a2 || a2 FROM t6;

-- Check comparisons.
SELECT true > true;
SELECT true > false;
SELECT false > true;
SELECT false > false;
SELECT true < true;
SELECT true < false;
SELECT false < true;
SELECT false < false;

SELECT a, true > a FROM t;
SELECT a, false > a FROM t;
SELECT a, true < a FROM t;
SELECT a, false < a FROM t;
SELECT a, a > true FROM t;
SELECT a, a > false FROM t;
SELECT a, a < true FROM t;
SELECT a, a < false FROM t;

SELECT a, a1, a > a1 FROM t, t6;
SELECT a, a1, a < a1 FROM t, t6;

SELECT true >= true;
SELECT true >= false;
SELECT false >= true;
SELECT false >= false;
SELECT true <= true;
SELECT true <= false;
SELECT false <= true;
SELECT false <= false;

SELECT a, true >= a FROM t;
SELECT a, false >= a FROM t;
SELECT a, true <= a FROM t;
SELECT a, false <= a FROM t;
SELECT a, a >= true FROM t;
SELECT a, a >= false FROM t;
SELECT a, a <= true FROM t;
SELECT a, a <= false FROM t;

SELECT a, a1, a >= a1 FROM t, t6;
SELECT a, a1, a <= a1 FROM t, t6;

SELECT true == true;
SELECT true == false;
SELECT false == true;
SELECT false == false;
SELECT true != true;
SELECT true != false;
SELECT false != true;
SELECT false != false;

SELECT a, true == a FROM t;
SELECT a, false == a FROM t;
SELECT a, true != a FROM t;
SELECT a, false != a FROM t;
SELECT a, a == true FROM t;
SELECT a, a == false FROM t;
SELECT a, a != true FROM t;
SELECT a, a != false FROM t;

SELECT a, a1, a == a1 FROM t, t6;
SELECT a, a1, a != a1 FROM t, t6;

SELECT true IN (true);
SELECT false IN (true);
SELECT true IN (false);
SELECT false IN (false);
SELECT true IN (true, false);
SELECT false IN (true, false);
SELECT true IN (SELECT a1 FROM t6);
SELECT false IN (SELECT a1 FROM t6);
SELECT true IN (SELECT a1 FROM t6 LIMIT 1);
SELECT false IN (SELECT a1 FROM t6 LIMIT 1);
SELECT true IN (1, 1.2, 'true', false);
SELECT false IN (1, 1.2, 'true', false);

SELECT a, a IN (true) FROM t;
SELECT a, a IN (false) FROM t;
SELECT a, a IN (true, false) FROM t;
SELECT a, a IN (SELECT a1 FROM t6 LIMIT 1) FROM t;
SELECT a, a IN (SELECT a1 FROM t6) FROM t;
SELECT a, a IN (1, 1.2, 'true', false) FROM t;

SELECT true BETWEEN true AND true;
SELECT false BETWEEN true AND true;
SELECT true BETWEEN false AND false;
SELECT false BETWEEN false AND false;
SELECT true BETWEEN true AND false;
SELECT false BETWEEN true AND false;
SELECT true BETWEEN false AND true;
SELECT false BETWEEN false AND true;

SELECT a, a BETWEEN true AND true FROM t;
SELECT a, a BETWEEN false AND false FROM t;
SELECT a, a BETWEEN true AND false FROM t;
SELECT a, a BETWEEN false AND true FROM t;

-- Check interaction of BOOLEAN and INTEGER.
CREATE TABLE t7 (b INT PRIMARY KEY);
INSERT INTO t7 VALUES (123);

SELECT true AND 2;
SELECT false AND 2;
SELECT true OR 2;
SELECT false OR 2;
SELECT 2 AND true;
SELECT 2 AND false;
SELECT 2 OR true;
SELECT 2 OR false;

SELECT a1, a1 AND 2 FROM t6
SELECT a1, a1 OR 2 FROM t6
SELECT a1, 2 AND a1 FROM t6
SELECT a1, 2 OR a1 FROM t6
SELECT a2, a2 AND 2 FROM t6
SELECT a2, a2 OR 2 FROM t6
SELECT a2, 2 AND a2 FROM t6
SELECT a2, 2 OR a2 FROM t6

SELECT b, true AND b FROM t7;
SELECT b, false AND b FROM t7;
SELECT b, true OR b FROM t7;
SELECT b, false OR b FROM t7;
SELECT b, b AND true FROM t7;
SELECT b, b AND false FROM t7;
SELECT b, b OR true FROM t7;
SELECT b, b OR false FROM t7;

SELECT a1, b, a1 AND b FROM t6, t7;
SELECT a1, b, a1 OR b FROM t6, t7;
SELECT a1, b, b AND a1 FROM t6, t7;
SELECT a1, b, b OR a1 FROM t6, t7;
SELECT a2, b, a2 AND b FROM t6, t7;
SELECT a2, b, a2 OR b FROM t6, t7;
SELECT a2, b, b AND a2 FROM t6, t7;
SELECT a2, b, b OR a2 FROM t6, t7;

SELECT true + 2;
SELECT false + 2;
SELECT true - 2;
SELECT false - 2;
SELECT true * 2;
SELECT false * 2;
SELECT true / 2;
SELECT false / 2;
SELECT true % 2;
SELECT false % 2;
SELECT 2 + true;
SELECT 2 + false;
SELECT 2 - true;
SELECT 2 - false;
SELECT 2 * true;
SELECT 2 * false;
SELECT 2 / true;
SELECT 2 / false;
SELECT 2 % true;
SELECT 2 % false;

SELECT a1, a1 + 2 FROM t6
SELECT a1, a1 - 2 FROM t6
SELECT a1, a1 * 2 FROM t6
SELECT a1, a1 / 2 FROM t6
SELECT a1, a1 % 2 FROM t6
SELECT a1, 2 + a1 FROM t6
SELECT a1, 2 - a1 FROM t6
SELECT a1, 2 * a1 FROM t6
SELECT a1, 2 / a1 FROM t6
SELECT a1, 2 % a1 FROM t6
SELECT a2, a2 + 2 FROM t6
SELECT a2, a2 - 2 FROM t6
SELECT a2, a2 * 2 FROM t6
SELECT a2, a2 / 2 FROM t6
SELECT a2, a2 % 2 FROM t6
SELECT a2, 2 + a2 FROM t6
SELECT a2, 2 - a2 FROM t6
SELECT a2, 2 * a2 FROM t6
SELECT a2, 2 / a2 FROM t6
SELECT a2, 2 % a2 FROM t6

SELECT b, true + b FROM t7;
SELECT b, false + b FROM t7;
SELECT b, true - b FROM t7;
SELECT b, false - b FROM t7;
SELECT b, true * b FROM t7;
SELECT b, false * b FROM t7;
SELECT b, true / b FROM t7;
SELECT b, false / b FROM t7;
SELECT b, true % b FROM t7;
SELECT b, false % b FROM t7;
SELECT b, b + true FROM t7;
SELECT b, b + false FROM t7;
SELECT b, b - true FROM t7;
SELECT b, b - false FROM t7;
SELECT b, b * true FROM t7;
SELECT b, b * false FROM t7;
SELECT b, b / true FROM t7;
SELECT b, b / false FROM t7;
SELECT b, b % true FROM t7;
SELECT b, b % false FROM t7;

SELECT a1, b, a1 + b FROM t6, t7;
SELECT a1, b, a1 - b FROM t6, t7;
SELECT a1, b, a1 * b FROM t6, t7;
SELECT a1, b, a1 / b FROM t6, t7;
SELECT a1, b, a1 % b FROM t6, t7;
SELECT a1, b, b + a1 FROM t6, t7;
SELECT a1, b, b - a1 FROM t6, t7;
SELECT a1, b, b * a1 FROM t6, t7;
SELECT a1, b, b / a1 FROM t6, t7;
SELECT a1, b, b % a1 FROM t6, t7;
SELECT a2, b, a2 + b FROM t6, t7;
SELECT a2, b, a2 - b FROM t6, t7;
SELECT a2, b, a2 * b FROM t6, t7;
SELECT a2, b, a2 / b FROM t6, t7;
SELECT a2, b, a2 % b FROM t6, t7;
SELECT a2, b, b + a2 FROM t6, t7;
SELECT a2, b, b - a2 FROM t6, t7;
SELECT a2, b, b * a2 FROM t6, t7;
SELECT a2, b, b / a2 FROM t6, t7;
SELECT a2, b, b % a2 FROM t6, t7;

SELECT true & 2;
SELECT false & 2;
SELECT true | 2;
SELECT false | 2;
SELECT true << 2;
SELECT false << 2;
SELECT true >> 2;
SELECT false >> 2;
SELECT 2 & true;
SELECT 2 & false;
SELECT 2 | true;
SELECT 2 | false;
SELECT 2 << true;
SELECT 2 << false;
SELECT 2 >> true;
SELECT 2 >> false;

SELECT a1, a1 & 2 FROM t6
SELECT a1, a1 | 2 FROM t6
SELECT a1, a1 << 2 FROM t6
SELECT a1, a1 >> 2 FROM t6
SELECT a1, 2 & a1 FROM t6
SELECT a1, 2 | a1 FROM t6
SELECT a1, 2 << a1 FROM t6
SELECT a1, 2 >> a1 FROM t6
SELECT a2, a2 & 2 FROM t6
SELECT a2, a2 | 2 FROM t6
SELECT a2, a2 << 2 FROM t6
SELECT a2, a2 >> 2 FROM t6
SELECT a2, 2 & a2 FROM t6
SELECT a2, 2 | a2 FROM t6
SELECT a2, 2 << a2 FROM t6
SELECT a2, 2 >> a2 FROM t6

SELECT b, true & b FROM t7;
SELECT b, false & b FROM t7;
SELECT b, true | b FROM t7;
SELECT b, false | b FROM t7;
SELECT b, true << b FROM t7;
SELECT b, false << b FROM t7;
SELECT b, true >> b FROM t7;
SELECT b, false >> b FROM t7;
SELECT b, b & true FROM t7;
SELECT b, b & false FROM t7;
SELECT b, b | true FROM t7;
SELECT b, b | false FROM t7;
SELECT b, b << true FROM t7;
SELECT b, b << false FROM t7;
SELECT b, b >> true FROM t7;
SELECT b, b >> false FROM t7;

SELECT a1, b, a1 & b FROM t6, t7;
SELECT a1, b, a1 | b FROM t6, t7;
SELECT a1, b, a1 << b FROM t6, t7;
SELECT a1, b, a1 >> b FROM t6, t7;
SELECT a1, b, b & a1 FROM t6, t7;
SELECT a1, b, b | a1 FROM t6, t7;
SELECT a1, b, b << a1 FROM t6, t7;
SELECT a1, b, b >> a1 FROM t6, t7;
SELECT a2, b, a2 & b FROM t6, t7;
SELECT a2, b, a2 | b FROM t6, t7;
SELECT a2, b, a2 << b FROM t6, t7;
SELECT a2, b, a2 >> b FROM t6, t7;
SELECT a2, b, b & a2 FROM t6, t7;
SELECT a2, b, b | a2 FROM t6, t7;
SELECT a2, b, b << a2 FROM t6, t7;
SELECT a2, b, b >> a2 FROM t6, t7;

SELECT true > 2;
SELECT false > 2;
SELECT true < 2;
SELECT false < 2;
SELECT 2 > true;
SELECT 2 > false;
SELECT 2 < true;
SELECT 2 < false;

SELECT a1, a1 > 2 FROM t6
SELECT a1, a1 < 2 FROM t6
SELECT a1, 2 > a1 FROM t6
SELECT a1, 2 < a1 FROM t6
SELECT a2, a2 > 2 FROM t6
SELECT a2, a2 < 2 FROM t6
SELECT a2, 2 > a2 FROM t6
SELECT a2, 2 < a2 FROM t6

SELECT b, true > b FROM t7;
SELECT b, false > b FROM t7;
SELECT b, true < b FROM t7;
SELECT b, false < b FROM t7;
SELECT b, b > true FROM t7;
SELECT b, b > false FROM t7;
SELECT b, b < true FROM t7;
SELECT b, b < false FROM t7;

SELECT a1, b, a1 > b FROM t6, t7;
SELECT a1, b, a1 < b FROM t6, t7;
SELECT a1, b, b > a1 FROM t6, t7;
SELECT a1, b, b < a1 FROM t6, t7;
SELECT a2, b, a2 > b FROM t6, t7;
SELECT a2, b, a2 < b FROM t6, t7;
SELECT a2, b, b > a2 FROM t6, t7;
SELECT a2, b, b < a2 FROM t6, t7;

SELECT true >= 2;
SELECT false >= 2;
SELECT true <= 2;
SELECT false <= 2;
SELECT 2 >= true;
SELECT 2 >= false;
SELECT 2 <= true;
SELECT 2 <= false;

SELECT a1, a1 >= 2 FROM t6
SELECT a1, a1 <= 2 FROM t6
SELECT a1, 2 >= a1 FROM t6
SELECT a1, 2 <= a1 FROM t6
SELECT a2, a2 >= 2 FROM t6
SELECT a2, a2 <= 2 FROM t6
SELECT a2, 2 >= a2 FROM t6
SELECT a2, 2 <= a2 FROM t6

SELECT b, true >= b FROM t7;
SELECT b, false >= b FROM t7;
SELECT b, true <= b FROM t7;
SELECT b, false <= b FROM t7;
SELECT b, b >= true FROM t7;
SELECT b, b >= false FROM t7;
SELECT b, b <= true FROM t7;
SELECT b, b <= false FROM t7;

SELECT a1, b, a1 >= b FROM t6, t7;
SELECT a1, b, a1 <= b FROM t6, t7;
SELECT a1, b, b >= a1 FROM t6, t7;
SELECT a1, b, b <= a1 FROM t6, t7;
SELECT a2, b, a2 >= b FROM t6, t7;
SELECT a2, b, a2 <= b FROM t6, t7;
SELECT a2, b, b >= a2 FROM t6, t7;
SELECT a2, b, b <= a2 FROM t6, t7;

SELECT true == 2;
SELECT false == 2;
SELECT true != 2;
SELECT false != 2;
SELECT 2 == true;
SELECT 2 == false;
SELECT 2 != true;
SELECT 2 != false;

SELECT a1, a1 == 2 FROM t6
SELECT a1, a1 != 2 FROM t6
SELECT a1, 2 == a1 FROM t6
SELECT a1, 2 != a1 FROM t6
SELECT a2, a2 == 2 FROM t6
SELECT a2, a2 != 2 FROM t6
SELECT a2, 2 == a2 FROM t6
SELECT a2, 2 != a2 FROM t6

SELECT b, true == b FROM t7;
SELECT b, false == b FROM t7;
SELECT b, true != b FROM t7;
SELECT b, false != b FROM t7;
SELECT b, b == true FROM t7;
SELECT b, b == false FROM t7;
SELECT b, b != true FROM t7;
SELECT b, b != false FROM t7;

SELECT a1, b, a1 == b FROM t6, t7;
SELECT a1, b, a1 != b FROM t6, t7;
SELECT a1, b, b == a1 FROM t6, t7;
SELECT a1, b, b != a1 FROM t6, t7;
SELECT a2, b, a2 == b FROM t6, t7;
SELECT a2, b, a2 != b FROM t6, t7;
SELECT a2, b, b == a2 FROM t6, t7;
SELECT a2, b, b != a2 FROM t6, t7;

SELECT true IN (0, 1, 2, 3);
SELECT false IN (0, 1, 2, 3);
SELECT true IN (SELECT b FROM t7);
SELECT false IN (SELECT b FROM t7);
SELECT a1, a1 IN (0, 1, 2, 3) FROM t6

SELECT true BETWEEN 0 and 10;
SELECT false BETWEEN 0 and 10;
SELECT a1, a1 BETWEEN 0 and 10 FROM t6;
SELECT a2, a2 BETWEEN 0 and 10 FROM t6;

-- Check interaction of BOOLEAN and NUMBER.
CREATE TABLE t8 (c NUMBER PRIMARY KEY);
INSERT INTO t8 VALUES (4.56);

SELECT true AND 2.3;
SELECT false AND 2.3;
SELECT true OR 2.3;
SELECT false OR 2.3;
SELECT 2.3 AND true;
SELECT 2.3 AND false;
SELECT 2.3 OR true;
SELECT 2.3 OR false;

SELECT a1, a1 AND 2.3 FROM t6
SELECT a1, a1 OR 2.3 FROM t6
SELECT a1, 2.3 AND a1 FROM t6
SELECT a1, 2.3 OR a1 FROM t6
SELECT a2, a2 AND 2.3 FROM t6
SELECT a2, a2 OR 2.3 FROM t6
SELECT a2, 2.3 AND a2 FROM t6
SELECT a2, 2.3 OR a2 FROM t6

SELECT c, true AND c FROM t8;
SELECT c, false AND c FROM t8;
SELECT c, true OR c FROM t8;
SELECT c, false OR c FROM t8;
SELECT c, c AND true FROM t8;
SELECT c, c AND false FROM t8;
SELECT c, c OR true FROM t8;
SELECT c, c OR false FROM t8;

SELECT a1, c, a1 AND c FROM t6, t8;
SELECT a1, c, a1 OR c FROM t6, t8;
SELECT a1, c, c AND a1 FROM t6, t8;
SELECT a1, c, c OR a1 FROM t6, t8;
SELECT a2, c, a2 AND c FROM t6, t8;
SELECT a2, c, a2 OR c FROM t6, t8;
SELECT a2, c, c AND a2 FROM t6, t8;
SELECT a2, c, c OR a2 FROM t6, t8;

SELECT true + 2.3;
SELECT false + 2.3;
SELECT true - 2.3;
SELECT false - 2.3;
SELECT true * 2.3;
SELECT false * 2.3;
SELECT true / 2.3;
SELECT false / 2.3;
SELECT true % 2.3;
SELECT false % 2.3;
SELECT 2.3 + true;
SELECT 2.3 + false;
SELECT 2.3 - true;
SELECT 2.3 - false;
SELECT 2.3 * true;
SELECT 2.3 * false;
SELECT 2.3 / true;
SELECT 2.3 / false;
SELECT 2.3 % true;
SELECT 2.3 % false;

SELECT a1, a1 + 2.3 FROM t6
SELECT a1, a1 - 2.3 FROM t6
SELECT a1, a1 * 2.3 FROM t6
SELECT a1, a1 / 2.3 FROM t6
SELECT a1, a1 % 2.3 FROM t6
SELECT a1, 2.3 + a1 FROM t6
SELECT a1, 2.3 - a1 FROM t6
SELECT a1, 2.3 * a1 FROM t6
SELECT a1, 2.3 / a1 FROM t6
SELECT a1, 2.3 % a1 FROM t6
SELECT a2, a2 + 2.3 FROM t6
SELECT a2, a2 - 2.3 FROM t6
SELECT a2, a2 * 2.3 FROM t6
SELECT a2, a2 / 2.3 FROM t6
SELECT a2, a2 % 2.3 FROM t6
SELECT a2, 2.3 + a2 FROM t6
SELECT a2, 2.3 - a2 FROM t6
SELECT a2, 2.3 * a2 FROM t6
SELECT a2, 2.3 / a2 FROM t6
SELECT a2, 2.3 % a2 FROM t6

SELECT c, true + c FROM t8;
SELECT c, false + c FROM t8;
SELECT c, true - c FROM t8;
SELECT c, false - c FROM t8;
SELECT c, true * c FROM t8;
SELECT c, false * c FROM t8;
SELECT c, true / c FROM t8;
SELECT c, false / c FROM t8;
SELECT c, true % c FROM t8;
SELECT c, false % c FROM t8;
SELECT c, c + true FROM t8;
SELECT c, c + false FROM t8;
SELECT c, c - true FROM t8;
SELECT c, c - false FROM t8;
SELECT c, c * true FROM t8;
SELECT c, c * false FROM t8;
SELECT c, c / true FROM t8;
SELECT c, c / false FROM t8;
SELECT c, c % true FROM t8;
SELECT c, c % false FROM t8;

SELECT a1, c, a1 + c FROM t6, t8;
SELECT a1, c, a1 - c FROM t6, t8;
SELECT a1, c, a1 * c FROM t6, t8;
SELECT a1, c, a1 / c FROM t6, t8;
SELECT a1, c, a1 % c FROM t6, t8;
SELECT a1, c, c + a1 FROM t6, t8;
SELECT a1, c, c - a1 FROM t6, t8;
SELECT a1, c, c * a1 FROM t6, t8;
SELECT a1, c, c / a1 FROM t6, t8;
SELECT a1, c, c % a1 FROM t6, t8;
SELECT a2, c, a2 + c FROM t6, t8;
SELECT a2, c, a2 - c FROM t6, t8;
SELECT a2, c, a2 * c FROM t6, t8;
SELECT a2, c, a2 / c FROM t6, t8;
SELECT a2, c, a2 % c FROM t6, t8;
SELECT a2, c, c + a2 FROM t6, t8;
SELECT a2, c, c - a2 FROM t6, t8;
SELECT a2, c, c * a2 FROM t6, t8;
SELECT a2, c, c / a2 FROM t6, t8;
SELECT a2, c, c % a2 FROM t6, t8;

SELECT true > 2.3;
SELECT false > 2.3;
SELECT true < 2.3;
SELECT false < 2.3;
SELECT 2.3 > true;
SELECT 2.3 > false;
SELECT 2.3 < true;
SELECT 2.3 < false;

SELECT a1, a1 > 2.3 FROM t6
SELECT a1, a1 < 2.3 FROM t6
SELECT a1, 2.3 > a1 FROM t6
SELECT a1, 2.3 < a1 FROM t6
SELECT a2, a2 > 2.3 FROM t6
SELECT a2, a2 < 2.3 FROM t6
SELECT a2, 2.3 > a2 FROM t6
SELECT a2, 2.3 < a2 FROM t6

SELECT c, true > c FROM t8;
SELECT c, false > c FROM t8;
SELECT c, true < c FROM t8;
SELECT c, false < c FROM t8;
SELECT c, c > true FROM t8;
SELECT c, c > false FROM t8;
SELECT c, c < true FROM t8;
SELECT c, c < false FROM t8;

SELECT a1, c, a1 > c FROM t6, t8;
SELECT a1, c, a1 < c FROM t6, t8;
SELECT a1, c, c > a1 FROM t6, t8;
SELECT a1, c, c < a1 FROM t6, t8;
SELECT a2, c, a2 > c FROM t6, t8;
SELECT a2, c, a2 < c FROM t6, t8;
SELECT a2, c, c > a2 FROM t6, t8;
SELECT a2, c, c < a2 FROM t6, t8;

SELECT true >= 2.3;
SELECT false >= 2.3;
SELECT true <= 2.3;
SELECT false <= 2.3;
SELECT 2.3 >= true;
SELECT 2.3 >= false;
SELECT 2.3 <= true;
SELECT 2.3 <= false;

SELECT a1, a1 >= 2.3 FROM t6
SELECT a1, a1 <= 2.3 FROM t6
SELECT a1, 2.3 >= a1 FROM t6
SELECT a1, 2.3 <= a1 FROM t6
SELECT a2, a2 >= 2.3 FROM t6
SELECT a2, a2 <= 2.3 FROM t6
SELECT a2, 2.3 >= a2 FROM t6
SELECT a2, 2.3 <= a2 FROM t6

SELECT c, true >= c FROM t8;
SELECT c, false >= c FROM t8;
SELECT c, true <= c FROM t8;
SELECT c, false <= c FROM t8;
SELECT c, c >= true FROM t8;
SELECT c, c >= false FROM t8;
SELECT c, c <= true FROM t8;
SELECT c, c <= false FROM t8;

SELECT a1, c, a1 >= c FROM t6, t8;
SELECT a1, c, a1 <= c FROM t6, t8;
SELECT a1, c, c >= a1 FROM t6, t8;
SELECT a1, c, c <= a1 FROM t6, t8;
SELECT a2, c, a2 >= c FROM t6, t8;
SELECT a2, c, a2 <= c FROM t6, t8;
SELECT a2, c, c >= a2 FROM t6, t8;
SELECT a2, c, c <= a2 FROM t6, t8;

SELECT true == 2.3;
SELECT false == 2.3;
SELECT true != 2.3;
SELECT false != 2.3;
SELECT 2.3 == true;
SELECT 2.3 == false;
SELECT 2.3 != true;
SELECT 2.3 != false;

SELECT a1, a1 == 2.3 FROM t6
SELECT a1, a1 != 2.3 FROM t6
SELECT a1, 2.3 == a1 FROM t6
SELECT a1, 2.3 != a1 FROM t6
SELECT a2, a2 == 2.3 FROM t6
SELECT a2, a2 != 2.3 FROM t6
SELECT a2, 2.3 == a2 FROM t6
SELECT a2, 2.3 != a2 FROM t6

SELECT c, true == c FROM t8;
SELECT c, false == c FROM t8;
SELECT c, true != c FROM t8;
SELECT c, false != c FROM t8;
SELECT c, c == true FROM t8;
SELECT c, c == false FROM t8;
SELECT c, c != true FROM t8;
SELECT c, c != false FROM t8;

SELECT a1, c, a1 == c FROM t6, t8;
SELECT a1, c, a1 != c FROM t6, t8;
SELECT a1, c, c == a1 FROM t6, t8;
SELECT a1, c, c != a1 FROM t6, t8;
SELECT a2, c, a2 == c FROM t6, t8;
SELECT a2, c, a2 != c FROM t6, t8;
SELECT a2, c, c == a2 FROM t6, t8;
SELECT a2, c, c != a2 FROM t6, t8;

SELECT true IN (0.1, 1.2, 2.3, 3.4);
SELECT false IN (0.1, 1.2, 2.3, 3.4);
SELECT a1 IN (0.1, 1.2, 2.3, 3.4) FROM t6 LIMIT 1;
SELECT a2 IN (0.1, 1.2, 2.3, 3.4) FROM t6 LIMIT 1;
SELECT true IN (SELECT c FROM t8);
SELECT false IN (SELECT c FROM t8);
SELECT a1 IN (SELECT c FROM t8) FROM t6 LIMIT 1;
SELECT a2 IN (SELECT c FROM t8) FROM t6 LIMIT 1;

SELECT true BETWEEN 0.1 and 9.9;
SELECT false BETWEEN 0.1 and 9.9;
SELECT a1, a1 BETWEEN 0.1 and 9.9 FROM t6;
SELECT a2, a2 BETWEEN 0.1 and 9.9 FROM t6;

-- Check interaction of BOOLEAN and TEXT.
CREATE TABLE t9 (d TEXT PRIMARY KEY);
INSERT INTO t9 VALUES ('AsdF');

SELECT true AND 'abc';
SELECT false AND 'abc';
SELECT true OR 'abc';
SELECT false OR 'abc';
SELECT 'abc' AND true;
SELECT 'abc' AND false;
SELECT 'abc' OR true;
SELECT 'abc' OR false;

SELECT a1, a1 AND 'abc' FROM t6;
SELECT a1, a1 OR 'abc' FROM t6;
SELECT a1, 'abc' AND a1 FROM t6;
SELECT a1, 'abc' OR a1 FROM t6;
SELECT a2, a2 AND 'abc' FROM t6;
SELECT a2, a2 OR 'abc' FROM t6;
SELECT a2, 'abc' AND a2 FROM t6;
SELECT a2, 'abc' OR a2 FROM t6;

SELECT d, true AND d FROM t9;
SELECT d, false AND d FROM t9;
SELECT d, true OR d FROM t9;
SELECT d, false OR d FROM t9;
SELECT d, d AND true FROM t9;
SELECT d, d AND false FROM t9;
SELECT d, d OR true FROM t9;
SELECT d, d OR false FROM t9;

SELECT a1, d, a1 AND d FROM t6, t9;
SELECT a1, d, a1 OR d FROM t6, t9;
SELECT a1, d, d AND a1 FROM t6, t9;
SELECT a1, d, d OR a1 FROM t6, t9;
SELECT a2, d, a2 AND d FROM t6, t9;
SELECT a2, d, a2 OR d FROM t6, t9;
SELECT a2, d, d AND a2 FROM t6, t9;
SELECT a2, d, d OR a2 FROM t6, t9;

SELECT true > 'abc';
SELECT false > 'abc';
SELECT true < 'abc';
SELECT false < 'abc';
SELECT 'abc' > true;
SELECT 'abc' > false;
SELECT 'abc' < true;
SELECT 'abc' < false;

SELECT d, true > d FROM t9;
SELECT d, false > d FROM t9;
SELECT d, true < d FROM t9;
SELECT d, false < d FROM t9;
SELECT d, d > true FROM t9;
SELECT d, d > false FROM t9;
SELECT d, d < true FROM t9;
SELECT d, d < false FROM t9;

SELECT a1, d, a1 > d FROM t6, t9;
SELECT a1, d, a1 < d FROM t6, t9;
SELECT a1, d, d > a1 FROM t6, t9;
SELECT a1, d, d < a1 FROM t6, t9;
SELECT a2, d, a2 > d FROM t6, t9;
SELECT a2, d, a2 < d FROM t6, t9;
SELECT a2, d, d > a2 FROM t6, t9;
SELECT a2, d, d < a2 FROM t6, t9;

SELECT true || 'abc';
SELECT false || 'abc';
SELECT 'abc' || false;
SELECT 'abc' || true;

SELECT d, true || d FROM t9;
SELECT d, false || d FROM t9;
SELECT d, d || false FROM t9;
SELECT d, d || true FROM t9;

SELECT d, a1 || d FROM t6, t9;
SELECT d, a2 || d FROM t6, t9;
SELECT d, d || a1 FROM t6, t9;
SELECT d, d || a2 FROM t6, t9;

--
-- Check special types of strings: 'TRUE', 'true', 'FALSE',
-- 'false'.
--
INSERT INTO t9 VALUES ('TRUE'), ('true'), ('FALSE'), ('false');

SELECT true AND 'TRUE';
SELECT false AND 'TRUE';
SELECT true OR 'TRUE';
SELECT false OR 'TRUE';
SELECT 'TRUE' AND true;
SELECT 'TRUE' AND false;
SELECT 'TRUE' OR true;
SELECT 'TRUE' OR false;

SELECT a1, a1 AND 'TRUE' FROM t6;
SELECT a1, a1 OR 'TRUE' FROM t6;
SELECT a1, 'TRUE' AND a1 FROM t6;
SELECT a1, 'TRUE' OR a1 FROM t6;
SELECT a2, a2 AND 'TRUE' FROM t6;
SELECT a2, a2 OR 'TRUE' FROM t6;
SELECT a2, 'TRUE' AND a2 FROM t6;
SELECT a2, 'TRUE' OR a2 FROM t6;

SELECT d, true AND d FROM t9 WHERE d = 'TRUE';
SELECT d, false AND d FROM t9 WHERE d = 'TRUE';
SELECT d, true OR d FROM t9 WHERE d = 'TRUE';
SELECT d, false OR d FROM t9 WHERE d = 'TRUE';
SELECT d, d AND true FROM t9 WHERE d = 'TRUE';
SELECT d, d AND false FROM t9 WHERE d = 'TRUE';
SELECT d, d OR true FROM t9 WHERE d = 'TRUE';
SELECT d, d OR false FROM t9 WHERE d = 'TRUE';

SELECT a1, d, a1 AND d FROM t6, t9 WHERE d = 'TRUE';
SELECT a1, d, a1 OR d FROM t6, t9 WHERE d = 'TRUE';
SELECT a1, d, d AND a1 FROM t6, t9 WHERE d = 'TRUE';
SELECT a1, d, d OR a1 FROM t6, t9 WHERE d = 'TRUE';
SELECT a2, d, a2 AND d FROM t6, t9 WHERE d = 'TRUE';
SELECT a2, d, a2 OR d FROM t6, t9 WHERE d = 'TRUE';
SELECT a2, d, d AND a2 FROM t6, t9 WHERE d = 'TRUE';
SELECT a2, d, d OR a2 FROM t6, t9 WHERE d = 'TRUE';

SELECT true AND 'true';
SELECT false AND 'true';
SELECT true OR 'true';
SELECT false OR 'true';
SELECT 'true' AND true;
SELECT 'true' AND false;
SELECT 'true' OR true;
SELECT 'true' OR false;

SELECT a1, a1 AND 'true' FROM t6;
SELECT a1, a1 OR 'true' FROM t6;
SELECT a1, 'true' AND a1 FROM t6;
SELECT a1, 'true' OR a1 FROM t6;
SELECT a2, a2 AND 'true' FROM t6;
SELECT a2, a2 OR 'true' FROM t6;
SELECT a2, 'true' AND a2 FROM t6;
SELECT a2, 'true' OR a2 FROM t6;

SELECT d, true AND d FROM t9 WHERE d = 'true';
SELECT d, false AND d FROM t9 WHERE d = 'true';
SELECT d, true OR d FROM t9 WHERE d = 'true';
SELECT d, false OR d FROM t9 WHERE d = 'true';
SELECT d, d AND true FROM t9 WHERE d = 'true';
SELECT d, d AND false FROM t9 WHERE d = 'true';
SELECT d, d OR true FROM t9 WHERE d = 'true';
SELECT d, d OR false FROM t9 WHERE d = 'true';

SELECT a1, d, a1 AND d FROM t6, t9 WHERE d = 'true';
SELECT a1, d, a1 OR d FROM t6, t9 WHERE d = 'true';
SELECT a1, d, d AND a1 FROM t6, t9 WHERE d = 'true';
SELECT a1, d, d OR a1 FROM t6, t9 WHERE d = 'true';
SELECT a2, d, a2 AND d FROM t6, t9 WHERE d = 'true';
SELECT a2, d, a2 OR d FROM t6, t9 WHERE d = 'true';
SELECT a2, d, d AND a2 FROM t6, t9 WHERE d = 'true';
SELECT a2, d, d OR a2 FROM t6, t9 WHERE d = 'true';

SELECT true AND 'FALSE';
SELECT false AND 'FALSE';
SELECT true OR 'FALSE';
SELECT false OR 'FALSE';
SELECT 'FALSE' AND true;
SELECT 'FALSE' AND false;
SELECT 'FALSE' OR true;
SELECT 'FALSE' OR false;

SELECT a1, a1 AND 'FALSE' FROM t6;
SELECT a1, a1 OR 'FALSE' FROM t6;
SELECT a1, 'FALSE' AND a1 FROM t6;
SELECT a1, 'FALSE' OR a1 FROM t6;
SELECT a2, a2 AND 'FALSE' FROM t6;
SELECT a2, a2 OR 'FALSE' FROM t6;
SELECT a2, 'FALSE' AND a2 FROM t6;
SELECT a2, 'FALSE' OR a2 FROM t6;

SELECT d, true AND d FROM t9 WHERE d = 'FALSE';
SELECT d, false AND d FROM t9 WHERE d = 'FALSE';
SELECT d, true OR d FROM t9 WHERE d = 'FALSE';
SELECT d, false OR d FROM t9 WHERE d = 'FALSE';
SELECT d, d AND true FROM t9 WHERE d = 'FALSE';
SELECT d, d AND false FROM t9 WHERE d = 'FALSE';
SELECT d, d OR true FROM t9 WHERE d = 'FALSE';
SELECT d, d OR false FROM t9 WHERE d = 'FALSE';

SELECT a1, d, a1 AND d FROM t6, t9 WHERE d = 'FALSE';
SELECT a1, d, a1 OR d FROM t6, t9 WHERE d = 'FALSE';
SELECT a1, d, d AND a1 FROM t6, t9 WHERE d = 'FALSE';
SELECT a1, d, d OR a1 FROM t6, t9 WHERE d = 'FALSE';
SELECT a2, d, a2 AND d FROM t6, t9 WHERE d = 'FALSE';
SELECT a2, d, a2 OR d FROM t6, t9 WHERE d = 'FALSE';
SELECT a2, d, d AND a2 FROM t6, t9 WHERE d = 'FALSE';
SELECT a2, d, d OR a2 FROM t6, t9 WHERE d = 'FALSE';

SELECT true AND 'false';
SELECT false AND 'false';
SELECT true OR 'false';
SELECT false OR 'false';
SELECT 'false' AND true;
SELECT 'false' AND false;
SELECT 'false' OR true;
SELECT 'false' OR false;

SELECT a1, a1 AND 'false' FROM t6;
SELECT a1, a1 OR 'false' FROM t6;
SELECT a1, 'false' AND a1 FROM t6;
SELECT a1, 'false' OR a1 FROM t6;
SELECT a2, a2 AND 'false' FROM t6;
SELECT a2, a2 OR 'false' FROM t6;
SELECT a2, 'false' AND a2 FROM t6;
SELECT a2, 'false' OR a2 FROM t6;

SELECT d, true AND d FROM t9 WHERE d = 'false';
SELECT d, false AND d FROM t9 WHERE d = 'false';
SELECT d, true OR d FROM t9 WHERE d = 'false';
SELECT d, false OR d FROM t9 WHERE d = 'false';
SELECT d, d AND true FROM t9 WHERE d = 'false';
SELECT d, d AND false FROM t9 WHERE d = 'false';
SELECT d, d OR true FROM t9 WHERE d = 'false';
SELECT d, d OR false FROM t9 WHERE d = 'false';

SELECT a1, d, a1 AND d FROM t6, t9 WHERE d = 'false';
SELECT a1, d, a1 OR d FROM t6, t9 WHERE d = 'false';
SELECT a1, d, d AND a1 FROM t6, t9 WHERE d = 'false';
SELECT a1, d, d OR a1 FROM t6, t9 WHERE d = 'false';
SELECT a2, d, a2 AND d FROM t6, t9 WHERE d = 'false';
SELECT a2, d, a2 OR d FROM t6, t9 WHERE d = 'false';
SELECT a2, d, d AND a2 FROM t6, t9 WHERE d = 'false';
SELECT a2, d, d OR a2 FROM t6, t9 WHERE d = 'false';

-- Cleaning.
DROP VIEW v;
DROP TABLE t9;
DROP TABLE t8;
DROP TABLE t7;
DROP TABLE t6;
DROP TABLE t5;
DROP TABLE t4;
DROP TABLE t3;
DROP TABLE t2;
DROP TABLE t1;
DROP TABLE t0;
DROP TABLE ts;
DROP TABLE t;
