-- life.lua
-- The benchmark to check the performance of array-like data
-- structures with RW access. John Horton Conway's "Game of Life"
-- cellular automaton.
-- For the details see:
-- https://en.wikipedia.org/wiki/Conway%27s_Game_of_Life
-- Original by Dave Bollinger <DBollinger@compuserve.com> posted
-- to lua-l:
-- http://lua-users.org/lists/lua-l/1999-12/msg00003.html
-- modified to use ANSI terminal escape sequences
-- modified to use for instead of while

local bench = require('bench').new(arg)

local write=io.write

ALIVE="O"	DEAD="-"

function delay() -- NOTE: SYSTEM-DEPENDENT, adjust as necessary
  for i=1,10000 do end
  -- local i=os.clock()+1 while(os.clock()<i) do end
end

function ARRAY2D(w,h)
  local t = {w=w,h=h}
  for y=1,h do
    t[y] = {}
    for x=1,w do
      t[y][x]=0
    end
  end
  return t
end

_CELLS = {}

-- give birth to a "shape" within the cell array
function _CELLS:spawn(shape,left,top)
  for y=0,shape.h-1 do
    for x=0,shape.w-1 do
      self[top+y][left+x] = shape[y*shape.w+x+1]
    end
  end
end

-- run the CA and produce the next generation
function _CELLS:evolve(next)
  local ym1,y,yp1,yi=self.h-1,self.h,1,self.h
  while yi > 0 do
    local xm1,x,xp1,xi=self.w-1,self.w,1,self.w
    while xi > 0 do
      local sum = self[ym1][xm1] + self[ym1][x] + self[ym1][xp1] +
                  self[y][xm1] + self[y][xp1] +
                  self[yp1][xm1] + self[yp1][x] + self[yp1][xp1]
      next[y][x] = ((sum==2) and self[y][x]) or ((sum==3) and 1) or 0
      xm1,x,xp1,xi = x,xp1,xp1+1,xi-1
    end
    ym1,y,yp1,yi = y,yp1,yp1+1,yi-1
  end
end

-- output the array to screen
function _CELLS:draw()
  local out="" -- accumulate to reduce flicker
  for y=1,self.h do
   for x=1,self.w do
      out=out..(((self[y][x]>0) and ALIVE) or DEAD)
    end
    out=out.."\n"
  end
  write(out)
end

-- constructor
function CELLS(w,h)
  local c = ARRAY2D(w,h)
  c.spawn = _CELLS.spawn
  c.evolve = _CELLS.evolve
  c.draw = _CELLS.draw
  return c
end

--
-- shapes suitable for use with spawn() above
--
HEART = { 1,0,1,1,0,1,1,1,1; w=3,h=3 }
GLIDER = { 0,0,1,1,0,1,0,1,1; w=3,h=3 }
EXPLODE = { 0,1,0,1,1,1,1,0,1,0,1,0; w=3,h=4 }
FISH = { 0,1,1,1,1,1,0,0,0,1,0,0,0,0,1,1,0,0,1,0; w=5,h=4 }
BUTTERFLY = { 1,0,0,0,1,0,1,1,1,0,1,0,0,0,1,1,0,1,0,1,1,0,0,0,1; w=5,h=5 }

-- the main routine
function LIFE(w,h)
  -- create two arrays
  local thisgen = CELLS(w,h)
  local nextgen = CELLS(w,h)

  -- create some life
  -- about 1000 generations of fun, then a glider steady-state
  thisgen:spawn(GLIDER,5,4)
  thisgen:spawn(EXPLODE,25,10)
  thisgen:spawn(FISH,4,12)

  -- run until break
  local gen=1
  write("\027[2J")	-- ANSI clear screen
  while 1 do
    thisgen:evolve(nextgen)
    thisgen,nextgen = nextgen,thisgen
    write("\027[H")	-- ANSI home cursor
    thisgen:draw()
    write("Life - generation ",gen,"\n")
    gen=gen+1
    if gen>2000 then break end
    --delay()		-- no delay
  end
  return thisgen
end

-- Result of the LIFE(40, 20) after 2000 generations.
--[[
----------------------------------------
----------------------------------------
--OO--------------------------O---------
-OO--------------------------O-O--------
---O--------------------------O---------
----------------------------------------
----------------------------------------
----------------------------------------
----------------------------------------
----------------------------------------
----------------------------------------
----------------------------------------
---O------------------------------------
--O-O-----------------------------------
--O-O-----------------------------------
---O------------------------------------
----------------------------------------
-------OO-------------------------------
-------OO-------------------------------
----------------------------------------
]]

local function check_life(thisgen, w, h)
  local expected_cells = ARRAY2D(w, h)
  for y = 1, h do
    for x = 1, w do
      expected_cells[y][x] = false
    end
  end
  local alive_cells = {
    {3, 3}, {3, 4}, {3, 31},
    {4, 2}, {4, 3}, {4, 30}, {4, 32},
    {5, 4}, {5, 31},
    {13, 4},
    {14, 3}, {14, 5},
    {15, 3}, {15, 5},
    {16, 4},
    {18, 8}, {18, 9},
    {19, 8}, {19, 9},
  }
  for _, cell in ipairs(alive_cells) do
    local y, x = cell[1], cell[2]
    expected_cells[y][x] = true
  end
  for y = 1, h do
    for x = 1, w do
      assert(thisgen[y][x] > 0 == expected_cells[y][x],
             ('Incorrect value for cell (%d, %d)'):format(x, y))
    end
  end
  return true
end

local stdout = io.output()

bench:add({
  name = 'life',
  setup = function()
    io.output('/dev/null')
  end,
  payload = function()
    return LIFE(40, 20)
  end,
  teardown = function()
    io.output(stdout)
  end,
  checker = function(res)
    return check_life(res, 40, 20)
  end,
  items = 2000 * 40 * 20,
})

bench:run_and_report()
