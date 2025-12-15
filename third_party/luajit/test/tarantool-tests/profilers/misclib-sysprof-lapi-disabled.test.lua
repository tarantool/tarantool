local tap = require('tap')
local test = tap.test('misclib-sysprof-lapi-disabled'):skipcond({
  ['Sysprof is enabled'] = not os.getenv('LUAJIT_DISABLE_SYSPROF'),
})

test:plan(9)

-- Attempt to start sysprof when sysprof is disabled.
local res, err, errno = misc.sysprof.start()
test:is(res, nil, 'result status on start when sysprof is disabled')
test:ok(err:match('profiler is disabled'),
        'error on start when sysprof is disabled')
test:ok(type(errno) == 'number', 'errno on start when sysprof is disabled')

-- Attempt to stop sysprof when sysprof is disabled.
res, err, errno = misc.sysprof.stop()
test:is(res, nil, 'result status on stop when sysprof is disabled')
test:ok(err:match('profiler is disabled'),
        'error on stop when sysprof is disabled')
test:ok(type(errno) == 'number', 'errno on start when sysprof is disabled')

-- Attempt to report when sysprof is disabled.
res, err, errno = misc.sysprof.report()
test:is(res, nil, 'result status on report when sysprof is disabled')
test:ok(err:match('profiler is disabled'),
        'error on stop when sysprof is disabled')
test:ok(type(errno) == 'number', 'errno on start when sysprof is disabled')

test:done(true)
