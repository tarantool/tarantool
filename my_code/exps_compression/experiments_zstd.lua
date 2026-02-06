local MIN_LEVEL = 1
local MAX_LEVEL = 22

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
    local p = io.popen(cmd, "r")
    if not p then
        error("failed to start command: " .. cmd)
    end
    for line in p:lines() do
        print("    " .. line)
    end
    local ok, why, code = p:close()
end

for level = MIN_LEVEL, MAX_LEVEL do
    print(string.format("  MEMTX_SORT_DATA_ZSTD_LEVEL = %d", level))

    -- SNAPSHOT
    local snap_cmd = string.format(
        '%s -e "local t=require(\'internal.tweaks\'); ' ..
        't.MEMTX_SORT_DATA_ZSTD_LEVEL=%d" ' ..
        '%s %s',
        TARANTOOL_BIN, level, SNAP_SCRIPT, SNAP_OPTS
    )

    print(string.format("[SNAPSHOT] zstd_level = %d", level))
    run(snap_cmd)

    -- RECOVERY
    local reco_cmd = string.format(
        '%s -e "local t=require(\'internal.tweaks\'); ' ..
        't.MEMTX_SORT_DATA_ZSTD_LEVEL=%d" ' ..
        '%s %s',
        TARANTOOL_BIN, level, RECO_SCRIPT, RECO_OPTS
    )
    
    print(string.format("[RECOVERY] zstd_level = %d", level))
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
print(string.format("DONE: MEMTX_SORT_DATA_ZSTD_LEVEL = %d..%d",
    MIN_LEVEL, MAX_LEVEL))
os.exit(0)
