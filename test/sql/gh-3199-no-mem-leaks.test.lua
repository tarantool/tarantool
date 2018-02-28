test_run = require('test_run').new()
fiber = require('fiber')

-- This test checks that no leaks of region memory happens during
-- executing SQL queries.
--

-- box.cfg()


box.sql.execute('CREATE TABLE test (id PRIMARY KEY, x INTEGER, y INTEGER)')
box.sql.execute('INSERT INTO test VALUES (1, 1, 1), (2, 2, 2)')
box.sql.execute('SELECT x, y, x + y FROM test ORDER BY y')

fiber.info()[fiber.self().id()].memory.used

box.sql.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.sql.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.sql.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.sql.execute('SELECT x, y, x + y FROM test ORDER BY y')

fiber.info()[fiber.self().id()].memory.used

box.sql.execute('CREATE TABLE test2 (id PRIMARY KEY, a TEXT, b INTEGER)')
box.sql.execute('INSERT INTO test2 VALUES (1, \'abc\', 1), (2, \'hello\', 2)')
box.sql.execute('INSERT INTO test2 VALUES (3, \'test\', 3), (4, \'xx\', 4)')
box.sql.execute('SELECT a, id + 2 * a, b FROM test2 WHERE b < id * 2 ORDER BY a ')

fiber.info()[fiber.self().id()].memory.used

box.sql.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')
box.sql.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')
box.sql.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')

fiber.info()[fiber.self().id()].memory.used

box.sql.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')
box.sql.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')
box.sql.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')

fiber.info()[fiber.self().id()].memory.used

-- Cleanup
box.sql.execute('DROP TABLE test')
box.sql.execute('DROP TABLE test2')

