
function join(inspector, n)
    local path = os.getenv('TARANTOOL_SRC_DIR')
    for i=1,n do
        local rid = tostring(i)
        os.execute('mkdir -p tmp')
        os.execute('cp '..path..'/test/replication/replica.lua ./tmp/replica'..rid..'.lua')
        os.execute('chmod +x ./tmp/replica'..rid..'.lua')
        local out_dir = box.cfg.wal_dir
        inspector:cmd("create server replica"..rid.." with rpl_master=default, script='"..out_dir.."/../tmp/replica"..rid..".lua'")
        inspector:cmd("start server replica"..rid)
    end
end


function call_all(callback)
    local all = box.space._cluster:select{}
    for _, tuple in pairs(all) do
        local id = tuple[1]
        if id ~= box.info.id then
            callback(id)
        end
    end
end

function unregister(inspector, id)
    box.space._cluster:delete{id}
end

function start(inspector, id)
    inspector:cmd('start server replica'..tostring(id - 1))
end

function stop(inspector, id)
    inspector:cmd('stop server replica'..tostring(id - 1))
end

function wait(inspector, id)
    inspector:wait_lsn('replica'..tostring(id - 1), 'default')
end

function delete(inspector, id)
    inspector:cmd('stop server replica'..tostring(id - 1))
    inspector:cmd('delete server replica'..tostring(id - 1))
end

function drop(inspector, id)
    unregister(inspector, id)
    delete(inspector, id)
end

function start_all(inspector)
    call_all(function (id) start(inspector, id) end)
end

function stop_all(inspector)
    call_all(function (id) stop(inspector, id) end)
end

function wait_all(inspector)
    call_all(function (id) wait(inspector, id) end)
end

function drop_all(inspector)
    call_all(function (id) drop(inspector, id) end)
end

function vclock_diff(left, right)
    local diff = 0
    for id, lsn in ipairs(left) do
        diff = diff + (right[id] or 0) - left[id]
    end
    for id, lsn in ipairs(right) do
        if left[id] == nil then
            diff = diff + right[id]
        end
    end
    return diff
end

return {
    join = join;
    start_all = start_all;
    stop_all = stop_all;
    wait_all = wait_all;
    drop_all = drop_all;
    vclock_diff = vclock_diff;
    unregister = unregister;
    delete = delete;
}
