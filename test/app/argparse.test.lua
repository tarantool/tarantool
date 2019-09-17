-- internal argparse test
test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua:<line>\"]: '")

argparse = require('internal.argparse').parse

-- test with empty arguments and options
argparse()
-- test with command name (should be excluded)
argparse({[0] = 'tarantoolctl', 'start', 'instance'})
-- test long option
argparse({'tarantoolctl', 'start', 'instance', '--start'})
argparse({'tarantoolctl', 'start', 'instance', '--start', '--stop'})
argparse({'tarantoolctl', 'start', 'instance', '--start', '--stop', '--stop'})
argparse({'tarantoolctl', 'start', 'instance', '--start', '--stop', '--stop'})
argparse({'tarantoolctl', 'start', 'instance', '-baobab'})
argparse({'tarantoolctl', 'start', 'instance', '-vovov'})
argparse({'tarantoolctl', 'start', 'instance', '--start=lalochka'})
argparse({'tarantoolctl', 'start', 'instance', '--start', 'lalochka'})
argparse({'tarantoolctl', 'start', 'instance', '--start', '--', 'lalochka'})
argparse({'tarantoolctl', 'start', 'instance', '--start', '-', 'lalochka'})
argparse({'--verh=42'}, {{'verh', 'number'}})
argparse({'--verh=42'}, {{'verh', 'number+'}})
argparse({'--verh=42'}, {{'verh', 'string'}})
argparse({'--verh=42'}, {{'verh', 'string+'}})
argparse({'--verh=42'}, {{'verh'}})
argparse({'--verh=42'}, {'verh'})
argparse({'--verh=42'}, {{'verh', 'boolean'}})
argparse({'--verh=42'}, {{'verh', 'boolean+'}})
argparse({'--verh=42'}, {'niz'})
argparse({'--super-option'})
argparse({'tarantoolctl', 'start', 'instance', '--start=lalochka', 'option', '-', 'another option'})

test_run:cmd("clear filter")
