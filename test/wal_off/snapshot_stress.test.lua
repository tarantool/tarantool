-- The test emulates account system. There are increasing number or accounts
-- and a lot of double entry transactions are made that moving random
-- ammount from random account to another random accont.
-- Snapshots are made every snapshot_interval seconds and then checked for consistency
env = require('test_run')
test_run = env.new()
-- Settings: You may increase theese value to make test longer
-- number of worker fibers:
workers_count = 80
-- number of iterations per fiber (operations + add new account + add space)
iteration_count = 8
-- number of operations per iterations
operation_count = 8
-- limit of random string length in every account
string_max_size = 128
-- initial number of accounts
accounts_start = 5
-- delay between snapshots
snapshot_interval = 0.005

fiber = require('fiber')
fio = require('fio')
log = require('log')

tarantool_bin_path = arg[-1]
work_dir = fio.cwd()
script_path = fio.pathjoin(work_dir, 'snap_script.lua')
cmd_template = [[/bin/sh -c 'cd "%s" && "%s" ./snap_script.lua 2> /dev/null']]
cmd = string.format(cmd_template, work_dir, tarantool_bin_path)

open_flags = {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}
script = fio.open(script_path, open_flags, tonumber('0777', 8))
script:write("os.exit(-1)")
script:close()
res = os.execute(cmd)
str_res = 'precheck ' .. (res ~= 0 and ' ok(1)' or 'failed(1)')
str_res

script = fio.open(script_path, open_flags, tonumber('0777', 8))
script:write("os.exit(0)")
script:close()
res = os.execute(cmd)
str_res = 'precheck ' .. (res == 0 and ' ok(2)' or 'failed(2)')
str_res

snap_search_wildcard = fio.pathjoin(box.cfg.memtx_dir, '*.snap');
snaps = fio.glob(snap_search_wildcard);
initial_snap_count = #snaps

if box.space.accounts then box.space.accounts:drop() end
if box.space.operations then box.space.operations:drop() end
if box.space.deleting then box.space.deleting:drop() end

s1 = box.schema.create_space("accounts")
i1 = s1:create_index('primary', { type = 'HASH', parts = {1, 'unsigned'} })
s2 = box.schema.create_space("operations")
i2 = s2:create_index('primary', { type = 'HASH', parts = {1, 'unsigned'} })
s3 = box.schema.create_space("deleting")
i3 = s3:create_index('primary', { type = 'TREE', parts = {1, 'unsigned'} })

n_accs = 0
n_ops = 0
n_spaces = 0
workers_done = 0

test_run:cmd("setopt delimiter ';'")
garbage = {};
str = ""
for i = 1,string_max_size do
    str = str .. '-' garbage[i - 1] = str
end;

function get_new_space_name()
    n_spaces = n_spaces + 1
    return "test" .. tostring(n_spaces - 1)
end;

tmp = get_new_space_name()
if box.space[tmp] then box.space[tmp]:drop() tmp = get_new_space_name() end
tmp = nil
n_spaces = 0

function get_rnd_acc()
    return math.floor(math.random() * n_accs)
end;

function get_rnd_val()
    return math.floor(math.random() * 10)
end;

function get_rnd_str()
    return garbage[math.floor(math.random() * string_max_size)]
end;

additional_spaces = { };

function add_space()
    local tmp_space = box.schema.create_space(get_new_space_name())
    table.insert(additional_spaces, tmp_space)
    tmp_space:create_index('test')
    n_spaces = n_spaces + 1
end;

function add_acc()
    s1:insert{n_accs, 0} n_accs = n_accs + 1
end;

function add_op(n1, n2, v)
    s2:insert{n_ops, n1, n2, v}
    n_ops = n_ops + 1
end;

function acc_add(n, v)
    s1:update({n}, {{'+', 2, v}, {'=', 3, get_rnd_str()}})
end;

function do_op(n1, n2, v)
    box.begin()
    add_op(n1, n2, v)
    acc_add(n1, v)
    acc_add(n2, -v)
    box.commit()
end;

function do_rand_op()
    do_op(get_rnd_acc(), get_rnd_acc(), get_rnd_val())
end;

function remove_smth()
    s3:delete{i3:min()[1]}
end;

function init()
    for i = 1,accounts_start do
        add_acc()
    end
    for i = 1,workers_count*iteration_count do
        s3:auto_increment{"I hate dentists!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"}
    end
end;

function work_itr()
    for j = 1,operation_count do
        do_rand_op()
        fiber.sleep(0)
    end
    add_acc()
    remove_smth()
    add_space()
end;

function work()
    for i = 1,iteration_count do
        if not pcall(work_itr) then
            log.info("work_itr failed")
            break
        end
    end
    workers_done = workers_done + 1
end;

snaps_done = false;
function snaps()
    repeat
        pcall(box.snapshot)
        fiber.sleep(snapshot_interval)
    until workers_done == workers_count
    snaps_done = true
end;

function wait()
    while (not snaps_done) do
        fiber.sleep(0.01)
    end
end;

init();

log.info('Part I: creating snapshot start');

for i = 1,workers_count do
    fiber.create(work)
end;
local tmp_fib = fiber.create(snaps);
wait();

log.info('Part I: creating snapshot done');

#s1:select{};
#s2:select{};

s1:drop();
s2:drop();
for k,v in pairs(additional_spaces) do v:drop() end;
s1 = nil s2 = nil additional_spaces = nil;

script_code = [[
fio = require'fio'
new_snap_dir = "]] .. fio.pathjoin(box.cfg.memtx_dir, "snap_test") .. [["
os.execute("mkdir " .. new_snap_dir)
os.execute("cp ]] .. fio.pathjoin(box.cfg.memtx_dir, "*.xlog") .. [[ " .. new_snap_dir .. "/")
os.execute("cp ]] .. fio.pathjoin(box.cfg.memtx_dir, "*.vylog") .. [[ " .. new_snap_dir .. "/")
os.execute("cp ]] .. fio.pathjoin(box.cfg.memtx_dir, "*.snap") .. [[ " .. new_snap_dir .. "/")
box.cfg{ memtx_memory = 536870912, memtx_dir = new_snap_dir, wal_dir = new_snap_dir, vinyl_dir = new_snap_dir, wal_mode = "none" }

log = require('log')

s1 = box.space.accounts
s2 = box.space.operations

total_sum = 0
t1 = {}
for k,v in s1:pairs() do t1[ v[1] ] = v[2] total_sum = total_sum + v[2] end
if total_sum ~= 0 then log.info('error: total sum mismatch') os.execute("rm -r " .. new_snap_dir) os.exit(-1) end

t2 = {}
function acc_inc(n1, v) t2[n1] = (t2[n1] and t2[n1] or 0) + v end
for k,v in s2:pairs() do acc_inc(v[2], v[4]) acc_inc(v[3], -v[4]) end

bad = false
for k,v in pairs(t1) do if (t2[k] and t2[k] or 0) ~= v then bad = true end end
for k,v in pairs(t2) do if (t1[k] and t1[k] or 0) ~= v then bad = true end end
if bad then log.info('error: operation apply mismatch') os.execute("rm -r " .. new_snap_dir) os.exit(-1) end

log.info('success: snapshot is ok')
os.execute("rm -r " .. new_snap_dir)
os.exit(0)
]];
script = fio.open(script_path, open_flags, tonumber('0777', 8))
script:write(script_code)
script:close()

log.info('Part II: checking snapshot start');

snaps = fio.glob(snap_search_wildcard);
snaps_find_status = #snaps <= initial_snap_count and 'where are my snapshots?' or 'snaps found';
snaps_find_status;
snapshot_check_failed = false
while #snaps > initial_snap_count do
    if not snapshot_check_failed and os.execute(cmd) ~= 0 then
        snapshot_check_failed = true
    end
    max_snap = nil
    for k,v in pairs(snaps) do
        if max_snap == nil or v > max_snap then
            max_snap = v
            max_snap_k = k
        end
    end
    if max_snap:sub(1, 1) ~= "/" then
        max_snap = fio.pathjoin(box.cfg.memtx_dir, max_snap)
    end
    fio.unlink(max_snap)
    max_vylog = fio.basename(max_snap, '.snap') .. '.vylog'
    max_vylog = fio.pathjoin(box.cfg.vinyl_dir, max_vylog)
    fio.unlink(max_vylog)
    snaps[max_snap_k] = nil
end;
snapshot_check_failed;

log.info('Part II: checking snapshot done');

test_run:cmd("setopt delimiter ''");


