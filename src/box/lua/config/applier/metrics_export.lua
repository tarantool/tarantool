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

    if was_configured and not has_http_server then
        log.warn('metrics.export: module http.server is not available, ' ..
                 'HTTP metrics export is disabled')
        return
    end

    if has_http_server then
        if next(export_cfg) == nil then
            get_role().stop()
        else
            get_role().apply(export_cfg)
        end
    end
end

return {
    name = 'metrics.export',
    apply = function() end,
    post_apply = post_apply,
}
