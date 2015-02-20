s = box.schema.space.create('rtreebench')
_ = s:create_index('primary')
_ = s:create_index('spatial', { type = 'rtree', unique = false, parts = {2, 'array'}})

n_records = 20000
n_iterations = 10000
n_neighbors = 10

file = io.open("rtree_benchmark.res", "w")
start = os.clock()

--# setopt delimiter ';'
for i = 1, n_records do
   s:insert{i,{180*math.random(),180*math.random()}}
end;

file:write(string.format("Elapsed time for inserting %d records: %d\n", n_records, os.clock() - start));

start = os.clock();
n = 0;
for i = 1, n_iterations do
   x = 180*math.random()
   y = 180*math.random()
   for k,v in s.index.spatial:pairs({x,y,x+1,y+1}, {iterator = 'LE'}) do
       n = n + 1
   end
end;
file:write(string.format("Elapsed time for %d belongs searches selecting %d records: %d\n", n_iterations, n, os.clock() - start));

start = os.clock();
n = 0
for i = 1, n_iterations do
   x = 180*math.random()
   y = 180*math.random()
   for k,v in pairs(s.index.spatial:select({x,y }, {limit = n_neighbors, iterator = 'NEIGHBOR'})) do
      n = n + 1
   end
end;
file:write(string.format("Elapsed time for %d nearest %d neighbors searches selecting %d records: %d\n", n_iterations, n_neighbors, n, os.clock() - start));

start = os.clock();
for i = 1, n_records do
    s:delete{i}
end;
file:write(string.format("Elapsed time for deleting  %d records: %d\n", n_records, os.clock() - start));

file:close();
s:drop();

