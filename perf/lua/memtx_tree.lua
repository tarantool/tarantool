--
-- The test measures run time of box_replace C function.
--
-- Output format (console):
-- <test-case> <rows-per-second>
--
-- NOTE: The test requires a C module. Set the BUILDDIR environment variable to
-- the tarantool build directory if using out-of-source build.
--

local clock = require('clock')
local fiber = require('fiber')
local fio = require('fio')
local ffi = require('ffi')
local log = require('log')
local tarantool = require('tarantool')
local benchmark = require('benchmark')

local USAGE = [[
   request_count <number, 10000000> - the amount of replaces to perform
   repetitions <number, 1>          - the number of benchmark repetitions

 Being run without options, this benchmark measures the run time of a replace
 single of a tuple with single unsigned key (random with linear distribution).

]]

local params = benchmark.argparse(arg, {
    {'request_count', 'number'},
    {'repetitions', 'number'},
}, USAGE)

local DEFAULT_COUNT = 10000000
local DEFAULT_REPETITIONS = 1

params.request_count = params.request_count or DEFAULT_COUNT
params.repetitions = params.repetitions or DEFAULT_REPETITIONS

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local MODULEPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua',
                                '?.' .. tarantool.build.mod_format)

package.cpath = MODULEPATH .. ';' .. package.cpath

local benchmark_box = require('benchmark_box_module')

box.cfg({
    memtx_memory = 8 * 1024 * 1024 * 1024,
    wal_mode = 'none',
})

local options = {request_count = params.request_count,
                 payload = {{type = 'unsigned'}}}

local bench = benchmark.new(params)

box.schema.space.create('test')
box.space.test:create_index('pk')

fiber.set_max_slice(9000)

TESTS = {
    {
        name = 'box_insert',
        func = function(options)
            benchmark_box.insert(box.space.test.id, options)
        end
    },
    {
        name = 'box_replace',
        func = function(options)
            benchmark_box.replace(box.space.test.id, options)
        end
    },
    {
        name = 'box_delete',
        func = function(options)
            benchmark_box.delete(box.space.test.id, 0, options)
        end
    },
    {
        name = 'box_get',
        func = function(options)
            benchmark_box.get(box.space.test.id, 0, options)
        end
    },
}

for _, distribution in pairs({'incremental', 'linear'}) do
    options.payload[1].distribution = distribution
    for i = 1, params.repetitions + 1 do
        for _, test in ipairs(TESTS) do
            box.begin()
            local real_time_start = clock.time()
            local cpu_time_start = clock.proc()
            test.func(options)
            local delta_real = clock.time() - real_time_start
            local delta_cpu = clock.proc() - cpu_time_start
            box.commit()

            -- The first run is warm-up.
            if i ~= 1 then
                bench:add_result(test.name .. '_' .. distribution, {
                    real_time = delta_real,
                    cpu_time = delta_cpu,
                    items = params.request_count,
                })
            end
        end
    end
end

bench:dump_results()

os.exit(0)
