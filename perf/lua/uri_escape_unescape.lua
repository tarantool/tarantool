-- It is recommended to run benchmark using taskset for stable results.
-- taskset -c 1 tarantool uri_escape_unescape.lua

local uri = require("uri")
local clock = require("clock")
local t = require("tarantool")

local params = require('internal.argparse').parse(arg, {
    {'output', 'string'},
    {'output_format', 'string'},
})

local bench = require('benchmark').init(params)

local _, _, build_type = string.match(t.build.target, "^(.+)-(.+)-(.+)$")
if build_type == "Debug" then
    print("WARNING: tarantool has built with enabled debug mode")
end

local escape_opts = {
    unreserved = uri.unreserved("A-Z"),
    plus = true,
}
-- Alternate reserved and unreserved symbols plus a space symbol.
local str = "A$B$C$D$E$F$G$H$I$J$K$L$M$N$O$P$Q$R$S$T$U$V$W$X$Y$Z$ #"
local encode_sample = string.rep(str, 10^3)
local decode_sample = uri.escape(encode_sample, escape_opts)

-- JIT compilation starts work after detecting "hot" paths in loops and
-- function's calls. It potentially will make execution time of first
-- iterations slower than next ones. To avoid such effect we can introduce
-- "warmup" iterations, disable JIT at all or make JIT compilation
-- aggressive from the beginning.
--   - hotloop=1 enables compilation in loops after the first iteration.
--   - hotexit=1 enables faster compilation for side traces after
--     exit the parent trace.
jit.opt.start("hotloop=1", "hotexit=1")

local CYCLES = 10^3

local tests = {
    ["uri.escape"] = function()
        for _ = 1, CYCLES do
            uri.escape(encode_sample)
        end
    end,
    ["uri.unescape"] = function()
        for _ = 1, CYCLES do
            uri.unescape(decode_sample)
        end
    end
}

local function run_test(testname, func)
    local real_time = clock.time()
    local cpu_time = clock.proc()
    func()
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time
    bench:add_result(testname, real_delta, cpu_delta, CYCLES)
end

for testname, test_func in pairs(tests) do
    run_test(testname, test_func)
end

bench:dump_results()
