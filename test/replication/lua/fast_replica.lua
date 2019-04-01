
function create(inspector, name, replica)
    replica = replica or 'replica'
    os.execute('echo "Creating replica '..name..'"')
    os.execute('mkdir -p tmp')
    os.execute('cp '..os.getenv('TARANTOOL_SRC_DIR')..'/test/replication/'..replica..'.lua ./tmp/'..name..'.lua')
    os.execute('chmod +x ./tmp/'..name..'.lua')
    inspector:cmd("create server "..name.." with rpl_master=default, script='"..box.cfg.wal_dir.."/../tmp/"..name..".lua'")
end

function join(inspector, name, n, replica)
    n = n or 1
    for i=1,n do
        local rid = tostring(i)
        if n == 1 then rid = '' end
        create(inspector, name..rid, replica)
        start(inspector, name..rid)
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
    id = id or 2
    if box.space._cluster:delete{id} then
        return true
    end
    return false
end

function id_to_str(id)
    local strnum
    if id == nil then
        strnum = ''
    else
        strnum = tostring(id - 1)
    end
    return strnum
end

-- replica commands

function start(inspector, name, id)
    return inspector:cmd('start server '..name..id_to_str(id))
end

function stop(inspector, name, id)
    return inspector:cmd('stop server '..name..id_to_str(id))
end

function cleanup(inspector, name, id)
    return inspector:cmd('cleanup server '..name..id_to_str(id))
end

function wait(inspector, name, id)
    return inspector:wait_lsn(name..id_to_str(id), 'default')
end

function delete(inspector, name, id)
    return inspector:cmd('delete server '..name..id_to_str(id))
end

-- replica modes

function hibernate(inspector, name, id)
    return stop(inspector, name, id) and cleanup(inspector, name, id)
end

function drop(inspector, name, id)
    return hibernate(inspector, name, id) and delete(inspector, name, id)
end

function prune(inspector, name, id)
    return unregister(inspector, id) and drop(inspector, name, id)
end

-- multi calls

function start_all(inspector, name)
    call_all(function (id) start(inspector, name, id) end)
end

function stop_all(inspector, name)
    call_all(function (id) stop(inspector, name, id) end)
end

function cleanup_all(inspector, name)
    call_all(function (id) cleanup(inspector, name, id) end)
end

function wait_all(inspector, name)
    call_all(function (id) wait(inspector, name, id) end)
end

function delete_all(inspector, name)
    call_all(function (id) delete(inspector, name, id) end)
end

function hibernate_all(inspector, name)
    call_all(function (id) hibernate(inspector, name, id) end)
end

function drop_all(inspector, name)
    call_all(function (id) drop(inspector, name, id) end)
end

function prune_all(inspector, name)
    call_all(function (id) prune(inspector, name, id) end)
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
    create = create;
    join = join;
    start = start;
    start_all = start_all;
    stop = stop;
    stop_all = stop_all;
    cleanup = cleanup;
    cleanup_all = cleanup_all;
    wait = wait;
    wait_all = wait_all;
    delete = delete;
    delete_all = delete_all;
    hibernate = hibernate;
    hibernate_all = hibernate_all;
    drop = drop;
    drop_all = drop_all;
    prune = prune;
    prune_all = prune_all;
    vclock_diff = vclock_diff;
    unregister = unregister;
}
