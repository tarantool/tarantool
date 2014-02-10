print('eselect')

s = box.schema.create_space('eselect', { temporary = true })
index = s:create_index('primary', { type = 'tree' })

for i = 1, 20 do s:insert({ i, 1, 2, 3 }) end


s.index[0]:eselect(nil, { iterator = 'ALL', offset = 0, limit = 4294967295 })
s.index[0]:eselect({}, { iterator = 'ALL', offset = 0, limit = 4294967295 })

s.index[0]:eselect(1)
s.index[0]:eselect(1, { iterator = box.index.EQ })
s.index[0]:eselect(1, { iterator = 'EQ' })
s.index[0]:eselect(1, { iterator = 'GE' })
s.index[0]:eselect(1, { iterator = 'GE', limit = 2 })
s.index[0]:eselect(1, { iterator = 'LE', limit = 2 })
s.index[0]:eselect(1, { iterator = 'GE', offset = 10, limit = 2 })

s.index[0]:eselect(1, { iterator = 'GE', grep = function(t) if math.fmod(t[0], 2) == 0 then return true end end, limit = 2 })
s.index[0]:eselect(1, { iterator = 'GE', grep = function(t) if math.fmod(t[0], 2) == 0 then return true end end, limit = 2, offset = 1 })
s.index[0]:eselect(1, { iterator = 'GE', grep = function(t) if math.fmod(t[0], 2) == 0 then return true end end, limit = 2, offset = 1, map = function(t) return { t[0] } end })


s:eselect(2)

s:drop()
