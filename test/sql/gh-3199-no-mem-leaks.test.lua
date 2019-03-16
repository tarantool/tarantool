test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')
fiber = require('fiber')

-- This test checks that no leaks of region memory happens during
-- executing SQL queries.
--

-- box.cfg()


box.execute('CREATE TABLE test (id INT PRIMARY KEY, x INTEGER, y INTEGER)')
box.execute('INSERT INTO test VALUES (1, 1, 1), (2, 2, 2)')
box.execute('SELECT x, y, x + y FROM test ORDER BY y')

fiber.info()[fiber.self().id()].memory.used

box.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.execute('SELECT x, y, x + y FROM test ORDER BY y')
box.execute('SELECT x, y, x + y FROM test ORDER BY y')

fiber.info()[fiber.self().id()].memory.used

box.execute('CREATE TABLE test2 (id INT PRIMARY KEY, a TEXT, b INTEGER)')
box.execute('INSERT INTO test2 VALUES (1, \'abc\', 1), (2, \'hello\', 2)')
box.execute('INSERT INTO test2 VALUES (3, \'test\', 3), (4, \'xx\', 4)')
box.execute('SELECT a, id + 2, b FROM test2 WHERE b < id * 2 ORDER BY a ')

fiber.info()[fiber.self().id()].memory.used

box.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')
box.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')
box.execute('SELECT a, id + 2 * b, a FROM test2 WHERE b < id * 2 ORDER BY a ')

fiber.info()[fiber.self().id()].memory.used

box.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')
box.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')
box.execute('SELECT x, y + 3 * b, b FROM test2, test WHERE b = x')

fiber.info()[fiber.self().id()].memory.used

-- Cleanup
box.execute('DROP TABLE test')
box.execute('DROP TABLE test2')

