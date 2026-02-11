local log = require('internal.config.utils.log')
local has_http_server, _ = pcall(require, 'http.server')

local role
local was_configured = false

local function get_role()
    if role == nil then
        role = require('roles.metrics-export')
    end
    return role
end

local function post_apply(config)
    local configdata = config._configdata
    local export_cfg = configdata:get('metrics.export',
                                {use_default = true}) or {}

    was_configured = was_configured or next(export_cfg) ~= nil

    if not has_http_server then
        if was_configured then
            log.warn('metrics.export: module http.server is not available, ' ..
                    'HTTP metrics export is disabled')
        end
        return
    end

    if next(export_cfg) == nil then
        -- Do not stop the role if it was never configured via `metrics.export`.
        -- It may be configured independently as `roles.metrics-export`.
        if was_configured then
            get_role().stop()
        end
    else
        get_role().apply(export_cfg)
    end
end

return {
    name = 'metrics.export',
    apply = function() end,
    post_apply = post_apply,
}
