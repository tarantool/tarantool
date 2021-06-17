local INSTANCE_ID = string.match(arg[0], "gh%-6035%-(.+)%.lua")

local function unix_socket(name)
    return "unix/:./" .. name .. '.sock';
end

require('console').listen(os.getenv('ADMIN'))

if INSTANCE_ID == "master" then
    box.cfg({
        listen = unix_socket("master"),
    })
elseif INSTANCE_ID == "replica1" then
    box.cfg({
        listen = unix_socket("replica1"),
        replication = {
            unix_socket("master"),
            unix_socket("replica1")
        },
        election_mode = 'voter'
    })
else
    assert(INSTANCE_ID == "replica2")
    box.cfg({
        replication = {
            unix_socket("master"),
        },
        election_mode = 'voter'
    })
end

box.once("bootstrap", function()
    box.schema.user.grant('guest', 'super')
end)
