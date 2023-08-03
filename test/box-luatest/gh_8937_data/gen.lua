-- This script can be used only with Tarantool without the fix for gh-8937 (e.g.
-- version 3.0.0-alpha1-62-g983a7ec21).

box.cfg{}

local s = box.schema.create_space('gh_8937_memtx')
box.space._index:insert{s.id, 0, "pk", "hash", {hint = true}, {{0, "unsigned"}}}

s = box.schema.create_space('gh_8937_vinyl', {engine = 'vinyl'})
box.space._index:insert{s.id, 0, "pk", "tree", {hint = true}, {{0, "unsigned"}}}

box.snapshot()

os.exit(0)
