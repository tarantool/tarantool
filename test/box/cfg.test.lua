--# push filter 'listen: .*' to 'primary: <uri>'
--# push filter 'admin: .*' to 'admin: <uri>'
box.cfg.nosuchoption = 1
t = {} for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t
-- must be read-only
box.cfg()
t = {} for k,v in pairs(box.cfg) do if type(v) ~= 'table' and type(v) ~= 'function' then table.insert(t, k..': '..tostring(v)) end end
t
--# clear filter
