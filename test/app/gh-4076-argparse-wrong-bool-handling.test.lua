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
args = {'--flag1', 'positional value', '--flag2'}
argparse(args, params)

--
-- When several 'boolean' arguments are passed, the result will be
-- `true` (just as for one such argument).
--
params = {}
params[1] = {'foo', 'boolean'}
args = {'--foo', '--foo'}
argparse(args, params)

--
-- When several 'boolean+' arguments are passed, the result will
-- be a list of `true` values.
--
params = {}
params[1] = {'foo', 'boolean+'}
args = {'--foo', '--foo'}
argparse(args, params)

params = {}
params[1] = {'foo', 'boolean+'}
args = {'--foo', 'positional value', '--foo'}
argparse(args, params)

--
-- When a value is provided for a 'boolean' / 'boolean+' option
-- using --foo=bar syntax, the error should state that a value is
-- not expected for this option.
--
params = {}
params[1] = {'foo', 'boolean'}
argparse({'--foo=bar'}, params)

params = {}
params[1] = {'foo', 'boolean+'}
argparse({'--foo=bar'}, params)

--
-- When parameter value was omitted, it was replaced internally
-- with boolean true, and sometimes was showed in error messages.
-- Now it is 'nothing'.
--
params = {}
params[1] = {'value', 'number'}
argparse({'--value'}, params)

params = {}
params[1] = {'value', 'string'}
argparse({'--value'}, params)

--
-- Verify that short 'boolean' and 'boolean+' options behaviour
-- is the same as for long options.
--
params = {}
params[1] = {'f', 'boolean'}
args = {'-f'}
argparse(args, params)
args = {'-f', '-f'}
argparse(args, params)

params = {}
params[1] = {'f', 'boolean+'}
args = {'-f'}
argparse(args, params)
args = {'-f', '-f'}
argparse(args, params)

test_run:cmd("clear filter")
