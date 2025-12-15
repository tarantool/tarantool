do --- Parse inline comparisions expressions, base.
  local f = {{n = 5}}
  local a = f[1].n
  assert(1 < a)
  assert(1 < (f[1].n))
  assert(1 < f[1].n)
end

do --- Parse inline comparisions expressions with not.
  local tt = { a = 1 }
  assert(not(0 >= tt.a))
end
