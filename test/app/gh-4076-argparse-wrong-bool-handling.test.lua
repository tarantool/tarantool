test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")
argparse = require('internal.argparse').parse
--
-- gh-4076: argparse incorrectly processed boolean parameters,
-- that led to problems with tarantoolctl usage.
--
params = {}
params[1] = {'flag1', 'boolean'}
params[2] = {'flag2', 'boolean'}
params[3] = {'flag3', 'boolean'}
params[4] = {'flag4', 'boolean'}
params[5] = {'flag5', 'boolean'}
args = {'--flag1', 'true', '--flag2', '1', '--flag3', 'false', '--flag4', '0', '--flag5', 'TrUe'}
argparse(args, params)

args = {'--flag1', 'abc'}
argparse(args, params)

--
-- When parameter value was omitted, it was replaced internally
-- with boolean true, and sometimes was showed in error messages.
-- Now it is 'nothing'.
--
params = {}
params[1] = {'value', 'number'}
argparse({'--value'}, params)

params[1][2] = 'string'
argparse({'--value'}, params)

test_run:cmd("clear filter")
