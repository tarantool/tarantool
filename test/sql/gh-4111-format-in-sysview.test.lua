-- Make sure, that it is possible to create a VIEW which
-- refers to "_v" space, i.e. to sysview engine.
-- Before gh-4111 was fixed, attempt to create such a view
-- failed due to lack of format in a space with sysview
-- engine.

test_run = require('test_run').new()

box.space._vspace.index[1]:count(1) > 0

box.execute([[CREATE VIEW t AS SELECT "name" FROM "_vspace" y]])
box.execute([[SELECT * from t WHERE "name" = 'T']])
box.execute([[DROP VIEW t]])
