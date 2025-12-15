local tap = require('tap')
local test = tap.test('misclib-memprof-lapi-disabled'):skipcond({
  ['Memprof is enabled'] = not os.getenv('LUAJIT_DISABLE_MEMPROF'),
})

test:plan(6)

-- Attempt to start memprof when it is disabled.
local res, err, errno = misc.memprof.start()
test:is(res, nil, 'result status on start when memprof is disabled')
test:ok(err:match('profiler is disabled'),
        'error on start when memprof is disabled')
test:ok(type(errno) == 'number', 'errno on start when memprof is disabled')

-- Attempt to stop memprof when it is disabled.
res, err, errno = misc.memprof.stop()
test:is(res, nil, 'result status on stop when memprof is disabled')
test:ok(err:match('profiler is disabled'),
        'error on stop when memprof is disabled')
test:ok(type(errno) == 'number', 'errno on start when memprof is disabled')

test:done(true)
