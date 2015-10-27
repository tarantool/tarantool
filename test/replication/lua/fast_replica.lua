
function join(inspector, n)
    for i=1,n do
        local rid = tostring(i)
        os.execute('mkdir -p tmp')
        os.execute('cp ../replication/replica.lua ./tmp/replica'..rid..'.lua')
        os.execute('chmod +x ./tmp/replica'..rid..'.lua')
        inspector:cmd("create server replica"..rid.." with rpl_master=default, script='./var/tmp/replica"..rid..".lua'")
        inspector:cmd("start server replica"..rid)
    end
end


function drop_all(inspector)
    local all = box.space._cluster:select{}
    for _, tuple in pairs(all) do
        local id = tuple[1]
        if id ~= box.info.server.id then
            box.space._cluster:delete{id}
            inspector:cmd('stop server replica'..tostring(id - 1))
            inspector:cmd('cleanup server replica'..tostring(id - 1))
        end
    end
end

return {
    join = join;
    drop_all = drop_all;
}
