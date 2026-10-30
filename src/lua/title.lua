local ffi = require('ffi')
ffi.cdef[[
    void title_update();
    const char *title_get();
    void title_set_interpretor_name(const char *);
    const char *title_get_interpretor_name();
    void title_set_script_name(const char *);
    const char *title_get_script_name();
    void title_set_custom(const char *);
    const char *title_get_custom();
    void title_set_status(const char *);
    const char *title_get_status();
]]

local title = {}

function title.update(kv)
	if type(kv) == 'string' then kv = {custom_title = kv} end
    if type(kv) ~= 'table' then return end
    if kv.interpretor_name ~= nil then
        ffi.C.title_set_interpretor_name(tostring(kv.interpretor_name))
    end
    if kv.script_name ~= nil then
        ffi.C.title_set_script_name(tostring(kv.script_name))
    end
    if kv.status ~= nil then
        ffi.C.title_set_status(tostring(kv.status))
    end
    if kv.custom_title ~= nil then
        ffi.C.title_set_custom(tostring(kv.custom_title))
    end
    if not kv.__defer_update then
        ffi.C.title_update()
    end
end

function title.get()
	local function S(s) return s~=nil and ffi.string(s) or nil end
    return S(ffi.C.title_get()), {
        interpretor_name = S(ffi.C.title_get_interpretor_name()),
        script_name = S(ffi.C.title_get_script_name()),
        status = S(ffi.C.title_get_status()),
        custom_title = S(ffi.C.title_get_custom())
    }
end

return title
