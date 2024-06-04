-- It is recommended to run benchmark using taskset for stable results.
-- taskset -c 1 tarantool uri_escape_unescape.lua

local uri = require("uri")
local clock = require("clock")
local t = require("tarantool")

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

local cycles = 10^3
local start = clock.monotonic()
for _ = 1, cycles do
    uri.escape(encode_sample)
end
local encode_time = cycles / (clock.monotonic() - start)

start = clock.monotonic()
for _ = 1, cycles do
    uri.unescape(decode_sample)
end
local decode_time = cycles / (clock.monotonic() - start)

print(("uri.escape   %.2f  runs/sec"):format(encode_time))
print(("uri.unescape %.2f  runs/sec"):format(decode_time))
