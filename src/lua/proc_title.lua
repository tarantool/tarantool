local ffi = require('ffi')
ffi.cdef[[
    void process_title_update();
    const char *process_title_get();
    void process_title_set_interpretor_name(const char *);
    const char *process_title_get_interpretor_name();
    void process_title_set_script_name(const char *);
    const char *process_title_get_script_name();
    void process_title_set_custom(const char *);
    const char *process_title_get_custom();
    void process_title_set_status(const char *);
    const char *process_title_get_status();
]]

local proc_title = {}

function proc_title.update(kv)
	if type(kv) == 'string' then kv = {custom_title = kv} end
    if type(kv) ~= 'table' then return end
    if kv.interpretor_name ~= nil then
        ffi.C.process_title_set_interpretor_name(tostring(kv.interpretor_name))
    end
    if kv.script_name ~= nil then
        ffi.C.process_title_set_script_name(tostring(kv.script_name))
    end
    if kv.status ~= nil then
        ffi.C.process_title_set_status(tostring(kv.status))
    end
    if kv.custom_title ~= nil then
        ffi.C.process_title_set_custom(tostring(kv.custom_title))
    end
    if not kv.__defer_update then
        ffi.C.process_title_update()
    end
end

function proc_title.get()
	local function S(s) return s~=nil and ffi.string(s) or nil end
    return S(ffi.C.process_title_get()), {
        interpretor_name = S(ffi.C.process_title_get_interpretor_name()),
        script_name = S(ffi.C.process_title_get_script_name()),
        status = S(ffi.C.process_title_get_status()),
        custom_title = S(ffi.C.process_title_get_custom())
    }
end

return proc_title
