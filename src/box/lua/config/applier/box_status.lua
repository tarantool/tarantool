local box_status_state = {
    watcher = nil,
}

local SCHEMA_VERSION_ALERT_KEY = 'schema_version_is_outdated'

local function set_schema_version_alert(current_version, latest_version)
    -- require() it here to avoid a circular dependency.
    local config = require('config')

    config._aboard:set({
        type = 'warn',
        message = (
            'The schema version %s is outdated, the latest ' ..
            'version is %s. Please, consider using box.schema.upgrade().')
            :format(current_version, latest_version),
    }, {
        key = SCHEMA_VERSION_ALERT_KEY,
    })
end

local function drop_schema_version_alert()
    -- require() it here to avoid a circular dependency.
    local config = require('config')

    config._aboard:drop(SCHEMA_VERSION_ALERT_KEY)
end

local function check_schema_version()
    local current_version = box.internal.dd_version()
    local latest_version = box.internal.latest_dd_version()
    if current_version < latest_version then
        set_schema_version_alert(current_version, latest_version)
    else
        drop_schema_version_alert()
    end
end

local function box_status_watcher()
    return box.watch('box.status', function(key)
        assert(key == 'box.status')
        check_schema_version()
    end)
end

local function apply(_config)
    check_schema_version()
    if box_status_state.watcher == nil then
        box_status_state.watcher = box_status_watcher()
    end
end

return {
    name = 'box_status',
    apply = apply,
}
