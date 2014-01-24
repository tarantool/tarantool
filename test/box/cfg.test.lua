box.cfg.nosuchoption = 1
t = {} for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t
-- must be read-only
box.cfg.reload()
t = {} for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t
