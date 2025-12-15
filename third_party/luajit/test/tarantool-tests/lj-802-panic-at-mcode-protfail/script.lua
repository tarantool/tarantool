jit.opt.start('hotloop=1')

-- Run a simple loop that triggers <mprotect> on trace assembling.
local a = 0
for i = 1, 3 do
  a = a + i
end

-- XXX: Just a simple contract output in case neither the panic at
-- <mprotect>, nor crash occurs (see for LUAJIT_UNPROTECT_MCODE in
-- lj_mcode.c for more info).
io.write(arg[1])
