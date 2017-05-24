-- test for xlog_reader module
-- consists of 3 parts:
-- 1) ok snap/xlog reader
-- 2) broken files reader (crc sum is invalid, bad header [version/type])
-- 3) before box.cfg and after box.cfg

fio  = require('fio')
fun  = require('fun')
json = require('json')
xlog = require('xlog').pairs
trun = require('test_run').new()

pattern_prefix = fio.pathjoin(os.getenv("SOURCEDIR"), "test/xlog/reader")

pattern_prefix_re = pattern_prefix:gsub("/", "\\/")
trun:cmd(("push filter '%s' to '%s'"):format(pattern_prefix_re, "<prefix>"))

pattern_ok_v12 = fio.pathjoin(pattern_prefix, "v12/")
pattern_ok_v13 = fio.pathjoin(pattern_prefix, "v13/")

trun:cmd("setopt delimiter ';'")
function collect_results(file)
    local val = {}
    for k, v in xlog(file) do
        table.insert(val, setmetatable(v, { __serialize = "map"}))
    end
    return val
end;

fun.iter({
    fio.pathjoin(pattern_ok_v12, '00000000000000000000.ok.snap'),
    fio.pathjoin(pattern_ok_v12, '00000000000000000000.ok.xlog'),
}):map(collect_results):totable();
collectgarbage('collect');
fun.iter({
    fio.pathjoin(pattern_ok_v13, '00000000000000000000.ok.snap'),
    fio.pathjoin(pattern_ok_v13, '00000000000000000000.ok.xlog'),
}):map(collect_results):totable();
collectgarbage('collect');

check_error = function(name, err)
    local path = fio.pathjoin(pattern_prefix, name)
    local stat, oerr = pcall(collect_results, path)
    if stat == true or not string.find(tostring(oerr), err) then
        return false, oerr
    end
    return true
end;
trun:cmd("setopt delimiter ''");

check_error("version.bad.xlog", "file format version")
check_error("format.bad.xlog", "not support 'SNOP' file type")
collect_results(fio.pathjoin(pattern_prefix, "crc.bad.xlog"))
collect_results(fio.pathjoin(pattern_prefix, "eof.bad.xlog"))

trun:cmd('clear filter')
