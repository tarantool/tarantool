# encoding: utf-8
import sys
print """
# A test case for Bug#1042738
# https://bugs.launchpad.net/tarantool/+bug/1042738
# Iteration over a non-unique TREE index
#"""

admin("for i=1, 1000 do box.space[1]:truncate() for j=1, 30 do box.insert(1, j, os.time(), 1) end; index = box.space[1].index[1]; count = 0; for k,v in index.next, index, nil do count = count+1; end; if count ~= 30 then print('bug at iteration ', i, ', count is ', count) end end")
admin("box.space[1]:truncate()")

print """
# A test case for Bug#1043858 server crash on lua stack overflow on CentOS
# 5.4
#"""
admin("for i = 1, 100000, 1 do box.space[1]:insert(i,i) end")
admin("local t1 = {box.select(1, 1)}")
admin("box.space[1]:truncate()")
