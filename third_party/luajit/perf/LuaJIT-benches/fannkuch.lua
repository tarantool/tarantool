-- The benchmark that checks the performance of operations on
-- small integers and vectors of integers and the performance of
-- inner loops of the benchmark. The benchmark finds the maximum
-- number of flips in the table needed for any permutation.
-- For the details see:
-- https://benchmarksgame-team.pages.debian.net/benchmarksgame/description/fannkuchredux.html

local bench = require("bench").new(arg)

local function fannkuch(n)
  local p, q, s, odd, check, maxflips = {}, {}, {}, true, 0, 0
  for i=1,n do p[i] = i; q[i] = i; s[i] = i end
  repeat
    -- Print max. 30 permutations.
    if check < 30 then
      if not p[n] then return maxflips end	-- Catch n = 0, 1, 2.
      check = check + 1
    end
    -- Copy and flip.
    local q1 = p[1]				-- Cache 1st element.
    if p[n] ~= n and q1 ~= 1 then		-- Avoid useless work.
      for i=2,n do q[i] = p[i] end		-- Work on a copy.
      local flips = 1			-- Flip ...
      while true do
	local qq = q[q1]
	if qq == 1 then				-- ... until 1st element is 1.
	  if flips > maxflips then maxflips = flips end -- New maximum?
	  break
	end
	q[q1] = q1
	if q1 >= 4 then
	  local i, j = 2, q1 - 1
	  repeat q[i], q[j] = q[j], q[i]; i = i + 1; j = j - 1; until i >= j
	end
	q1 = qq
	flips=flips+1
      end
    end
    -- Permute.
    if odd then
      p[2], p[1] = p[1], p[2]; odd = false	-- Rotate 1<-2.
    else
      p[2], p[3] = p[3], p[2]; odd = true	-- Rotate 1<-2 and 1<-2<-3.
      for i=3,n do
	local sx = s[i]
	if sx ~= 1 then s[i] = sx-1; break end
	if i == n then return maxflips end	-- Out of permutations.
	s[i] = i
	-- Rotate 1<-...<-i+1.
	local t=p[1]; for j=i+1,1,-1 do p[j],t=t,p[j] end
      end
    end
  until false
end

local n = tonumber(arg and arg[1]) or 11

-- Precomputed numbers taken from "Performing Lisp Analysis of the
-- FANNKUCH Benchmark":
-- https://dl.acm.org/doi/pdf/10.1145/382109.382124
local FANNKUCH = { 0, 1, 2, 4, 7, 10, 16, 22, 30, 38, 51, 65, 80 }

local function factorial(n)
  local fact = 1
  for i = 2, n do
    fact = fact * i
  end
  return fact
end

bench:add({
  name = "fannkuch",
  payload = function()
    return fannkuch(n)
  end,
  checker = function(res)
    if n > #FANNKUCH then
      -- Not precomputed, so can't check.
      return true
    else
      return res == FANNKUCH[n]
    end
  end,
  -- Assume that we count permutations here.
  items = factorial(n),
})

bench:run_and_report()
