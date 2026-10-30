local uri = require("uri")
local clock = require("clock")
local t = require("tarantool")
local benchmark = require("benchmark")

local USAGE = [[
 This benchmark measures the performance of URI decoding and encoding
 for the chunk with many symbols to be escaped.
]]

local params = benchmark.argparse(arg, {}, USAGE)

local bench = benchmark.new(params)

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

local CYCLES = 10^4

local tests = {{
    name = "uri.escape",
    payload = function()
        for _ = 1, CYCLES do
            uri.escape(encode_sample)
        end
    end,
}, {
    name = "uri.unescape",
    payload = function()
        for _ = 1, CYCLES do
            uri.unescape(decode_sample)
        end
    end,
}}

local function run_test(testname, func)
    local real_time = clock.time()
    local cpu_time = clock.proc()
    func()
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time
    bench:add_result(testname, {
        real_time = real_delta,
        cpu_time = cpu_delta,
        items = CYCLES,
    })
end

for _, test in ipairs(tests) do
    run_test(test.name, test.payload)
end

bench:dump_results()
