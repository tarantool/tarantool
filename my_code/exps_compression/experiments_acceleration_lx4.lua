local MIN_ACCEL = 1
local MAX_ACCEL = 15

local TARANTOOL_BIN = "./src/tarantool"
local SNAP_SCRIPT   = "../perf/lua/box_snapshot.lua"
local RECO_SCRIPT   = "../perf/lua/recovery.lua"

local SNAP_OPTS = table.concat({
    "--engine=memtx",
    "--index=TREE",
    "--sk_count=1",
    "--row_count=10000000",
    "--column_count=3",
    "--memtx_sort_data",
    "--checkpoint_count=1",
}, " ")

local RECO_OPTS = table.concat({
    "--engine=memtx",
    "--index=TREE",
    "--sk_count=1",
    "--row_count=10000000",
    "--wal_row_count=0",
    "--wal_replace_count=0",
    "--column_count=3",
    "--memtx_sort_data",
    "--recovery_count=1",
}, " ")

local function run(cmd)
    -- print("  CMD: " .. cmd)
    local p = io.popen(cmd, "r")
    if not p then
        error("failed to start command: " .. cmd)
    end
    for line in p:lines() do
        print("    " .. line)
    end
    local ok, why, code = p:close()
    -- print(string.format("  EXIT: ok=%s, why=%s, code=%s",
        -- tostring(ok), tostring(why), tostring(code)))
end

for accel = MIN_ACCEL, MAX_ACCEL do
    print(string.format("  MEMTX_SORT_DATA_LZ4_ACCELERATION = %d", accel))

    local snap_cmd = string.format(
        '%s -e "local t=require(\'internal.tweaks\'); ' ..
        't.MEMTX_SORT_DATA_LZ4_ACCELERATION=%d" ' ..
        '%s %s',
        TARANTOOL_BIN, accel, SNAP_SCRIPT, SNAP_OPTS
    )

    print(string.format("[SNAPSHOT] accel = %d", accel))
    run(snap_cmd)
    
    local reco_cmd = string.format(
        '%s -e "local t=require(\'internal.tweaks\'); ' ..
        't.MEMTX_SORT_DATA_LZ4_ACCELERATION=%d" ' ..
        '%s %s',
        TARANTOOL_BIN, accel, RECO_SCRIPT, RECO_OPTS
    )
    
    print(string.format("[RECOVERY] accel = %d", accel))
    run(reco_cmd)
    local reco_cnd_2 = string.format('du -ha /home/sandrech/tarantool/build/recovery,engine=memtx,index=TREE,nohint=false,sk_count=1,column_count=3,row_count=10000000,wal_row_count=0,wal_replace_count=0')
    print(string.format("[SORTDATA MEMORY USAGE]"))
    run(reco_cnd_2) 



    local delete_cmd_recovery = string.format('rm -rf /home/sandrech/tarantool/build/recovery,engine=memtx,index=TREE,nohint=false,sk_count=1,column_count=3,row_count=10000000,wal_row_count=0,wal_replace_count=0')
    local delete_cmd_snap = string.format('rm -rf /home/sandrech/tarantool/build/box_snapshot,engine=memtx,index=TREE,nohint=false,sk_count=1,row_count=10000000,column_count=3')
    run(delete_cmd_recovery)
    run(delete_cmd_snap)
end

print()
print(string.format("DONE: MEMTX_SORT_DATA_LZ4_ACCELERATION = %d..%d",
    MIN_ACCEL, MAX_ACCEL))
os.exit(0)
