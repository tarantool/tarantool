local t = require('luatest')
local yaml = require('yaml')

local g = t.group()

g.test_yaml_merge_keys_basic = function()
    local cfg = yaml.decode([[
.standby_storage_dummy: &standby_storage
  labels: [ standby ]
  database:
    mode: ro

.active_storage_dummy: &active_storage
  labels: [ active ]
  database:
    mode: rw

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: *active_storage
            iproto:
              listen: []
          s-002:
            <<: *standby_storage
            iproto:
              listen: []
]])

    local instances = cfg.groups['g-001'].replicasets['r-001'].instances
    t.assert_equals(instances['s-001'].labels, {'active'})
    t.assert_equals(instances['s-001'].database.mode, 'rw')
    t.assert_equals(instances['s-001'].iproto.listen, {})

    t.assert_equals(instances['s-002'].labels, {'standby'})
    t.assert_equals(instances['s-002'].database.mode, 'ro')
    t.assert_equals(instances['s-002'].iproto.listen, {})
end

g.test_yaml_merge_keys_nested = function()
    local cfg = yaml.decode([[
.database_base: &db_base
  engine: memtx
  page_size: 8192

.something_else: &something_else
  <<: *db_base
  mode: ro

.active_storage_dummy: &active_storage
  labels: [ active ]
  database:
    mode: rw
    <<: *something_else

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: *active_storage
            iproto:
              listen: []
]])

    local instance = cfg.groups['g-001'].replicasets['r-001'].instances['s-001']
    t.assert_equals(instance.labels, {'active'})
    t.assert_equals(instance.database.mode, 'rw')
    t.assert_equals(instance.database.engine, 'memtx')
    t.assert_equals(instance.database.page_size, 8192)
    t.assert_equals(instance.iproto.listen, {})
end

g.test_yaml_merge_keys_sequence_mappings = function()
    local cfg = yaml.decode([[
.common: &common
  labels: [ base ]
  iproto:
    listen: []

.db_one: &db_one
  mode: ro
  page_size: 4096

.db_two: &db_two
  mode: rw
  engine: vinyl

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: *common
            database:
              <<: [ *db_one, *db_two ]
]])

    local instance = cfg.groups['g-001'].replicasets['r-001'].instances['s-001']
    t.assert_equals(instance.labels, {'base'})
    t.assert_equals(instance.database.mode, 'ro')
    t.assert_equals(instance.database.page_size, 4096)
    t.assert_equals(instance.database.engine, 'vinyl')
    t.assert_equals(instance.iproto.listen, {})
end

g.test_yaml_merge_keys_explicit_overrides_merged = function()
    local cfg = yaml.decode([[
.common: &common
  labels: [ base ]
  database:
    mode: ro
    page_size: 4096

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: *common
            database:
              mode: rw
]])

    local instance = cfg.groups['g-001'].replicasets['r-001'].instances['s-001']
    t.assert_equals(instance.labels, {'base'})
    t.assert_equals(instance.database.mode, 'rw')
    t.assert_equals(instance.database.page_size, nil)
end


g.test_yaml_merge_keys_sequence_non_mapping_value = function()
    t.assert_error_msg_contains('merge sequence contains non-mapping value',
        function()
            yaml.decode([[
.common: &common
  labels: [ base ]

.broken: &broken
  value: x

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: [ *common, 1, *broken ]
]])
        end)
end

g.test_yaml_merge_keys_non_mapping_value = function()
    t.assert_error_msg_contains('merge value must be a mapping or sequence',
        function()
            yaml.decode([[
.common: &common
  labels: [ base ]

groups:
  g-001:
    replicasets:
      r-001:
        instances:
          s-001:
            <<: 1
            labels: [ active ]
]])
        end)
end
