local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_all(function(g)
    g.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        election_mode = 'manual',
        replication = {
            server.build_listen_uri('server_a', g.replica_set.id),
            server.build_listen_uri('server_b', g.replica_set.id),
        },
    }
    g.server_a = g.replica_set:build_and_add_server{
        alias = 'server_a', box_cfg = box_cfg}
    g.server_b = g.replica_set:build_and_add_server{
        alias = 'server_b', box_cfg = box_cfg}
    g.replica_set:start()
    g.replica_set:wait_for_fullmesh()
    g.server_b:exec(function() box.ctl.promote() end)
    g.server_b:wait_for_election_leader()
end)

g.after_all(function(g)
    g.replica_set:drop()
end)

local function re_promote_server(server)
    server:exec(function() box.ctl.demote() end)
    server:wait_for_election_state('follower')
    server:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            box.ctl.promote()
        end)
    end)
    server:wait_for_election_leader()
end

local function format_node(node_label, node_id, node_addr)
    local representation
    if node_addr ~= nil then
        representation = string.format('%s (id = %s, addr: %s)',
                                       node_label, node_id, node_addr)
    else
        representation = string.format('%s (id = %s)',
                                      node_label, node_id)
    end
    return representation:gsub('([%-%(%)])', '%%%1')
end

local function get_node_identity(server)
    local id = server:get_instance_id()
    local uuid = server:get_instance_uuid()
    local name = server:exec(function()
        return box.info.name ~= box.NULL and box.info.name or nil
    end)
    return name, uuid, id
end

local function get_remote_node_info(server, opts)
    local name, uuid, id = get_node_identity(server)
    local is_ephemeral = opts and opts.is_ephemeral or false
    local addr = is_ephemeral and 'unix/:(socket)' or
        'unix/:' .. server.net_box_uri
    return format_node(name or uuid, id, addr)
end

local function get_local_node_info(server)
    local name, uuid, id = get_node_identity(server)
    return format_node(name or uuid, id)
end

g.test_raft_messages_contain_extended_node_info = function(g)
    local local_b_info = get_local_node_info(g.server_b)
    t.assert(g.server_b:grep_log(string.format(
        'RAFT: vote for %s', local_b_info)))

    re_promote_server(g.server_a)
    local local_a_info = get_local_node_info(g.server_a)
    local remote_a_info = get_remote_node_info(g.server_a)
    local remote_b_info = get_remote_node_info(g.server_b)

    t.assert(g.server_b:grep_log(string.format(
             'RAFT: vote for %s', remote_a_info)))
    t.assert(g.server_a:grep_log(string.format(
             'RAFT: vote for %s', local_a_info)))
    t.assert(g.server_a:grep_log(string.format(
        'RAFT: message.*from %s', remote_b_info)))
    t.assert(g.server_b:grep_log(string.format(
        'RAFT: leader is %s', remote_a_info)))
    t.assert(g.server_a:grep_log('RAFT: enter leader state'))
end

g.test_server_info_prefers_name_over_uuid = function(g)
    local remote_a_info = get_remote_node_info(g.server_a)
    -- Now the remote_a_info contains uuid.
    t.assert(g.server_b:grep_log(string.format(
        'RAFT: vote for %s', remote_a_info)))

    g.server_a:exec(function() box.cfg{instance_name = 'server_a'} end)
    re_promote_server(g.server_a)

    -- After setting the instance_name the remote_a_info will contain name,
    -- because name is more readable than uuid.
    local remote_a_info_with_name = get_remote_node_info(g.server_a)
    t.assert_not_equals(remote_a_info, remote_a_info_with_name)
    t.assert(g.server_b:grep_log(string.format(
        'RAFT: vote for %s', remote_a_info_with_name)))
end
