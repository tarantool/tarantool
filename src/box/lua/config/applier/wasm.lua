local luawasm = require('luawasm')
local tarantool = require('tarantool')
local log = require('internal.config.utils.log')

local function apply(config)
    local cfg =
        config._configdata:get('wasm.components', {use_default = true}) or {}
    box.wasm = box.wasm or {}
    if next(cfg) ~= nil and not tarantool.build.wasm then
        log.warn('WebAssembly support is not available. ' ..
                 'Please rebuild Tarantool with -DTARANTOOL_WASM=ON. ' ..
                 'The luawasm module may still run, but crashes are ' ..
                 'possible during graceful shutdown.')
    end
    box.wasm.components = luawasm.load_components(cfg)
    function box.wasm.get(name)
        return box.wasm.components[name]
    end
end

return {
    name = 'wasm',
    apply = apply,
}
