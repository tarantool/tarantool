local t = require('luatest')
local cbuilder = require('test.config-luatest.cbuilder')
local replicaset = require('test.config-luatest.replicaset')

local g = t.group()

g.before_all(replicaset.init)
g.after_each(replicaset.drop)
g.after_all(replicaset.clean)

-- Ease writing of a long error message in a code.
local function toline(s)
    return s:gsub('\n', ''):gsub(' +', ' '):strip()
end

-- Verify that an anonymous replica can be started and joined to
-- a replicaset.
--
-- This test excludes the `replication.peers` autoconstruction
-- logic by defining the option on its own. This automatic
-- construction is verified in a separate test case.
g.test_basic = function(g)
    -- One master, two replicas, two anonymous replicas.
    local config = cbuilder.new()
        :set_replicaset_option('replication.peers', {
            'replicator:secret@unix/:./instance-001.iproto',
            'replicator:secret@unix/:./instance-002.iproto',
            'replicator:secret@unix/:./instance-003.iproto',
        })
        :add_instance('instance-001', {
            database = {
                mode = 'rw',
            },
        })
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-005', {
            replication = {
                anon = true,
            },
        })
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()

    -- Verify that all the instances are healthy.
    replicaset:each(function(server)
        server:exec(function()
            t.assert_equals(box.info.status, 'running')
        end)
    end)

    -- Verify that the anonymous replicas are actually anonymous.
    replicaset['instance-004']:exec(function()
        t.assert_equals(box.info.id, 0)
    end)
    replicaset['instance-005']:exec(function()
        t.assert_equals(box.info.id, 0)
    end)
end

-- Verify that an anonymous replica is not used as an upstream for
-- other instances of the same replicaset.
g.test_no_anonymous_upstream = function(g)
    -- One master, two replicas, two anonymous replicas.
    --
    -- NB: It is the same as config in `test_basic`, but has no
    -- `replication.peers` option.
    local config = cbuilder.new()
        :add_instance('instance-001', {
            database = {
                mode = 'rw',
            },
        })
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-005', {
            replication = {
                anon = true,
            },
        })
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()

     -- Verify that the anonymous replicas are actually anonymous.
    replicaset['instance-004']:exec(function()
        t.assert_equals(box.info.id, 0)
    end)
    replicaset['instance-005']:exec(function()
        t.assert_equals(box.info.id, 0)
    end)

    -- Verify box.cfg.replication on all instances.
    --
    -- NB: We can check box.info.replication as well, but it just
    -- adds extra code and doesn't improve coverage of upstream
    -- list construction logic.
    replicaset:each(function(server)
        server:exec(function()
            t.assert_equals(box.cfg.replication, {
                'replicator:secret@unix/:./instance-001.iproto',
                'replicator:secret@unix/:./instance-002.iproto',
                'replicator:secret@unix/:./instance-003.iproto',
                -- No instance-{004,005}, because they're
                -- anonymous replicas.
            })
        end)
    end)
end

-- Verify that anonymous replicas are skipped when choosing a
-- bootstrap leader in the `failover: supervised` mode.
g.test_supervised_mode_bootstrap_leader_not_anon = function(g)
    -- `failover: supervised` assigns a first non-anonymous
    -- instance as a bootstrap leader. The order is alphabetical.
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'supervised')
        :add_instance('instance-001', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {})
        :add_instance('instance-005', {
            replication = {
                anon = true,
            },
        })
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()

    -- Verify that instance-001 (anonymous replica) is
    -- successfully started. An attempt to make it writable (to
    -- use as a bootstrap leader) would lead to a startup error.
    t.helpers.retrying({timeout = 60}, function()
        replicaset['instance-001']:exec(function()
            t.assert_equals(box.info.status, 'running')
        end)
    end)

    -- Verify that instance-002 is the bootstrap leader.
    --
    -- NB: An instance with box.info.id = 1 is a bootstrap leader.
    replicaset['instance-002']:exec(function()
        t.assert_equals(box.info.id, 1)
    end)
end

-- Verify that a replicaset with all the instances configured as
-- anonymous replicas refuse to start with a meaningful error
-- message.
g.test_all_anonymous = function(g)
    local config = cbuilder.new()
        :add_instance('instance-001', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-002', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-003', {
            replication = {
                anon = true,
            },
        })
        :config()

    replicaset.startup_error(g, config, toline([[
        All the instances of replicaset "replicaset-001" of group "group-001"
        are configured as anonymous replicas; it effectively means that the
        whole replicaset is read-only; moreover, it means that default
        replication.peers construction logic will create empty upstream list
        and each instance is de-facto isolated: neither is connected to any
        other; this configuration is forbidden, because it looks like there
        is no meaningful use case
    ]]))
end

-- Verify that an anonymous replica can't be configured in
-- read-write mode.
--
-- The whole replicaset refuses to start if this misconfiguration
-- is found.
--
-- replication.failover: off
g.test_anonymous_replica_rw_mode = function(g)
    local config = cbuilder.new()
        :add_instance('instance-001', {
            database = {
                mode = 'rw',
            },
        })
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {
            database = {
                mode = 'rw',
            },
            replication = {
                anon = true,
            },
        })
        :config()

    replicaset.startup_error(g, config, toline([[
        database.mode = "rw" is set for instance "instance-004" of replicaset
        "replicaset-001" of group "group-001", but this option cannot be used
        together with replication.anon = true
    ]]))
end

-- Verify that an anonymous replica can't be assigned as a leader.
--
-- The whole replicaset refuses to start if this misconfiguration
-- is found.
--
-- replication.failover: manual
g.test_anonymous_replica_leader = function(g)
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'instance-004')
        :add_instance('instance-001', {})
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {
            replication = {
                anon = true,
            },
        })
        :config()

    replicaset.startup_error(g, config, toline([[
        replication.anon = true is set for instance "instance-004" of replicaset
        "replicaset-001" of group "group-001" that is configured as a leader; a
        leader can not be an anonymous replica
    ]]))
end

-- Verify that an anonymous replica can't be configured with
-- replication.election_mode parameter other than null or "off".
--
-- The whole replicaset refuses to start if this misconfiguration
-- is found.
--
-- replication.failover: election
g.test_anonymous_replica_election_mode_other_than_off = function(g)
    local error_t = toline([[
        replication.election_mode = %q is set for instance "instance-004" of
        replicaset "replicaset-001" of group "group-001", but this option cannot
        be used together with replication.anon = true; consider setting
        replication.election_mode = "off" explicitly for this instance
    ]])

    for _, election_mode in ipairs({'candidate', 'voter', 'manual'}) do
        local config = cbuilder.new()
            :set_replicaset_option('replication.failover', 'election')
            :add_instance('instance-001', {})
            :add_instance('instance-002', {})
            :add_instance('instance-003', {})
            :add_instance('instance-004', {
                replication = {
                    anon = true,
                    election_mode = election_mode,
                },
            })
            :config()

        replicaset.startup_error(g, config, error_t:format(election_mode))
    end
end

-- Verify that the election mode defaults to 'off' for an
-- anonymous replica in a replicaset with election failover.
g.test_anonymous_replica_election_mode_off = function(g)
    -- Three non-anonymous instances, two anonymous replicas.
    --
    -- The replicaset is in `failover: election` mode.
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'election')
        :add_instance('instance-001', {})
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :add_instance('instance-004', {
            replication = {
                anon = true,
            },
        })
        :add_instance('instance-005', {
            replication = {
                anon = true,
            },
        })
        :config()

    local replicaset = replicaset.new(g, config)
    replicaset:start()

    local function verify_election_mode_candidate()
        t.assert_equals(box.cfg.election_mode, 'candidate')
    end

    local function verify_election_mode_off()
        t.assert_equals(box.cfg.election_mode, 'off')
    end

    -- Verify that non-anonymous instances have election mode
    -- 'candidate', while anonymous replicas are 'off'.
    replicaset['instance-001']:exec(verify_election_mode_candidate)
    replicaset['instance-002']:exec(verify_election_mode_candidate)
    replicaset['instance-003']:exec(verify_election_mode_candidate)
    replicaset['instance-004']:exec(verify_election_mode_off)
    replicaset['instance-005']:exec(verify_election_mode_off)
end

-- Verify that an anonymous replica can join a replicaset that has
-- all the instances in read-only mode.
g.test_join_anonymous_replica_to_all_ro_replicaset = function(g)
    local config = cbuilder.new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'instance-001')
        :add_instance('instance-001', {})
        :add_instance('instance-002', {})
        :add_instance('instance-003', {})
        :config()

    -- Bootstrap the replicaset.
    local replicaset = replicaset.new(g, config)
    replicaset:start()

    -- Unset the leader -- make all the instances read-only.
    local new_config = cbuilder.new(config)
        :set_replicaset_option('leader', nil)
        :config()
    replicaset:reload(new_config)

    -- Verify that the instances actually enter read-only mode.
    replicaset:each(function(server)
        server:exec(function()
            t.assert_equals(box.info.ro, true)
        end)
    end)

    -- Add a new anonymous replica into the config and reflect it
    -- in the replicaset object. Start the replica.
    local new_config_2 = cbuilder.new(new_config)
        :add_instance('instance-004', {
            replication = {
                anon = true,
            },
        })
        :config()
    replicaset:sync(new_config_2)
    replicaset:start_instance('instance-004')

    -- Verify that the new instance is an anonymous replica and
    -- that it is synchronized with instance-{001,002,003}.
    t.helpers.retrying({timeout = 60}, function()
        replicaset['instance-004']:exec(function()
            t.assert_equals(box.info.id, 0)
            t.assert_equals(box.info.status, 'running')
            t.assert_equals(box.space._cluster:count(), 3)
        end)
    end)
end
