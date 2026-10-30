box.schema.role.create('iddqd')
box.schema.role.create('iddqd')
box.schema.role.drop('iddqd')
box.schema.role.drop('iddqd')
box.schema.role.create('iddqd')
-- impossible to su to a role
box.session.su('iddqd')
-- test granting privilege to a role
box.schema.role.grant('iddqd', 'execute', 'universe')
box.schema.role.info('iddqd')
box.schema.role.revoke('iddqd', 'execute', 'universe')
box.schema.role.info('iddqd')
-- test granting a role to a user
box.schema.user.create('tester')
box.schema.user.info('tester')
box.schema.user.grant('tester', 'execute', 'role', 'iddqd')
box.schema.user.info('tester')
-- test granting user to a user
box.schema.user.grant('tester', 'execute', 'role', 'tester')
-- test granting a non-execute grant on a role - error
box.schema.user.grant('tester', 'write', 'role', 'iddqd')
box.schema.user.grant('tester', 'read', 'role', 'iddqd')
-- test granting role to a role
box.schema.role.grant('iddqd', 'execute', 'role', 'iddqd')
box.schema.role.grant('iddqd', 'iddqd')
box.schema.role.revoke('iddqd', 'iddqd')
box.schema.user.grant('tester', 'iddqd')
box.schema.user.revoke('tester', 'iddqd')
box.schema.role.drop('iddqd')
box.schema.user.revoke('tester', 'no-such-role')
box.schema.user.grant('tester', 'no-such-role')
box.schema.user.drop('tester')
-- check for loops in role grants
box.schema.role.create('a')
box.schema.role.create('b')
box.schema.role.create('c')
box.schema.role.create('d')
box.schema.role.grant('b', 'a')
box.schema.role.grant('c', 'a')
box.schema.role.grant('d', 'b')
box.schema.role.grant('d', 'c')

--check user restrictions
box.schema.user.grant('a', 'd')
box.schema.user.revoke('a', 'd')
box.schema.user.drop('a')

box.schema.role.grant('a', 'd')
box.schema.role.drop('d')
box.schema.role.drop('b')
box.schema.role.drop('c')
box.schema.role.drop('a')
-- check that when dropping a role, it's first revoked
-- from whoever it is granted
box.schema.role.create('a')
box.schema.role.create('b')
box.schema.role.grant('b', 'a')
box.schema.role.drop('a')
box.schema.role.info('b')
box.schema.role.drop('b')
-- check a grant received via a role
box.schema.user.create('test')
box.schema.user.create('grantee')
box.schema.role.create('liaison')

--check role restrictions
box.schema.role.grant('test', 'liaison')
box.schema.role.revoke('test', 'liaison')
box.schema.role.drop('test')

box.schema.user.grant('grantee', 'liaison')
box.schema.user.grant('test', 'read,write,create', 'universe')
box.session.su('test')
s = box.schema.space.create('test')
_ = s:create_index('i1')
box.schema.role.grant('liaison', 'read,write', 'space', 'test')
box.session.su('grantee')
box.space.test:insert{1}
box.space.test:select{1}
box.session.su('test')
box.schema.role.revoke('liaison', 'read,write', 'space', 'test')
box.session.su('grantee')
box.space.test:insert{1}
box.space.test:select{1}
box.session.su('admin')
box.schema.user.drop('test')
box.schema.user.drop('grantee')
box.schema.role.drop('liaison')


--
-- Test how privileges are propagated through a complex role graph.
-- Here's the graph:
--
-- role1 ->- role2 -->- role4 -->- role6 ->- user1
--                \               /     \
--                 \->- role5 ->-/       \->- role9 ->- role10 ->- user
--                     /     \               /
--           role3 ->-/       \->- role7 ->-/
--
-- Privilege checks verify that grants/revokes are propagated correctly
-- from the role1 to role10.
--
box.schema.user.create("user")
box.schema.role.create("role1")
box.schema.role.create("role2")
box.schema.role.create("role3")
box.schema.role.create("role4")
box.schema.role.create("role5")
box.schema.role.create("role6")
box.schema.role.create("role7")
box.schema.user.create("user1")
box.schema.role.create("role9")
box.schema.role.create("role10")

box.schema.role.grant("role2", "role1")
box.schema.role.grant("role4", "role2")
box.schema.role.grant("role5", "role2")
box.schema.role.grant("role5", "role3")
box.schema.role.grant("role6", "role4")
box.schema.role.grant("role6", "role5")
box.schema.role.grant("role7", "role5")
box.schema.user.grant("user1", "role6")
box.schema.role.grant("role9", "role6")
box.schema.role.grant("role9", "role7")
box.schema.role.grant("role10", "role9")
box.schema.user.grant("user", "role10")

-- try to create a cycle
box.schema.role.grant("role2", "role10")

--
-- test grant propagation
--
box.schema.role.grant("role1", "read", "universe")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.session.su("admin")
box.schema.role.revoke("role1", "read", "universe")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.session.su("admin")

--
-- space-level privileges
--
box.schema.role.grant("role1", "read", "space", "_index")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.space._index:get{288, 0}[3]
box.session.su("admin")
box.schema.role.revoke("role1", "read", "space", "_index")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.space._index:get{288, 0}[3]
box.session.su("admin")

--
-- grant to a non-leaf branch
--
box.schema.role.grant("role5", "read", "space", "_index")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.space._index:get{288, 0}[3]
box.session.su("admin")
box.schema.role.revoke("role5", "read", "space", "_index")
box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.space._index:get{288, 0}[3]
box.session.su("admin")

--
-- grant via two branches
--
box.schema.role.grant("role3", "read", "space", "_index")
box.schema.role.grant("role4", "read", "space", "_index")
box.schema.role.grant("role9", "read", "space", "_index")

box.session.su("user")
box.space._index:get{288, 0}[3]
box.session.su("user1")
box.space._index:get{288, 0}[3]

box.session.su("admin")
box.schema.role.revoke("role3", "read", "space", "_index")

box.session.su("user")
box.space._index:get{288, 0}[3]
box.session.su("user1")
box.space._index:get{288, 0}[3]

box.session.su("admin")
box.schema.role.revoke("role4", "read", "space", "_index")

box.session.su("user")
box.space._index:get{288, 0}[3]
box.session.su("user1")
box.space._index:get{288, 0}[3]

box.session.su("admin")
box.schema.role.revoke("role9", "read", "space", "_index")

box.session.su("user")
box.space._index:get{288, 0}[3]
box.session.su("user1")
box.space._index:get{288, 0}[3]
box.session.su("admin")

--
-- check diamond-shaped grant graph
--

box.schema.role.grant("role5", "read", "space", "_space")

box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.session.su("user1")
box.space._space.index.name:get{"_space"}[3]

box.session.su("admin")
box.schema.role.revoke("role5", "read", "space", "_space")

box.session.su("user")
box.space._space.index.name:get{"_space"}[3]
box.session.su("user1")
box.space._space.index.name:get{"_space"}[3]

box.session.su("admin")

box.schema.user.drop("user")
box.schema.user.drop("user1")
box.schema.role.drop("role1")
box.schema.role.drop("role2")
box.schema.role.drop("role3")
box.schema.role.drop("role4")
box.schema.role.drop("role5")
box.schema.role.drop("role6")
box.schema.role.drop("role7")
box.schema.role.drop("role9")
box.schema.role.drop("role10")


--
-- only the creator of the role can grant it (or a superuser)
-- There is no grant option.
-- the same applies for privileges
--
box.schema.user.create('user')
box.schema.user.create('grantee')

box.schema.user.grant('user', 'read,write,execute,create', 'universe')
box.session.su('user')
box.schema.role.create('role')
box.session.su('admin')
box.schema.user.grant('grantee', 'role')
box.schema.user.revoke('grantee', 'role')
box.schema.user.create('john')
box.session.su('john')
-- error
box.schema.user.grant('grantee', 'role')
--
box.session.su('admin')
_ = box.schema.space.create('test')
box.schema.user.grant('john', 'read,write,execute', 'universe')
box.session.su('john')
box.schema.user.grant('grantee', 'role')
box.schema.user.grant('grantee', 'read', 'space', 'test')
--
-- granting 'public' is however an exception - everyone
-- can grant 'public' role, it's implicitly granted with
-- a grant option.
--
box.schema.user.grant('grantee', 'public')
--
-- revoking role 'public' is another deal - only the
-- superuser can do that, and even that would be useless,
-- since one can still re-grant it back to oneself.
--
box.schema.user.revoke('grantee', 'public')

box.session.su('admin')
box.schema.user.drop('john')
box.schema.user.drop('user')
box.schema.user.drop('grantee')
box.schema.role.drop('role')
box.space.test:drop()

--
-- grant a privilege through a role, but
-- the user has another privilege either granted
-- natively (one case) or via another role.
-- Check that privileges actually OR, but
-- not replace each other.
--
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
box.schema.user.create('john')
box.schema.user.grant('john', 'read', 'space', 'test')
box.session.su('john')
box.space.test:select{}
box.space.test:insert{1}
box.session.su('admin')
box.schema.role.grant('public', 'write', 'space', 'test')
box.session.su('john')
box.space.test:select{}
box.space.test:insert{2}
box.session.su('admin')
box.schema.role.revoke('public', 'write', 'space', 'test')
box.session.su('john')
box.space.test:select{}
box.space.test:insert{1}
box.session.su('admin')
box.space.test:drop()
box.schema.user.drop('john')

-- test ER_GRANT
box.space._priv:replace{1, 0, 'universe', 0, 0}
-- role.exists()
--
-- true if the role is present
box.schema.role.exists('public')
-- for if there is no such role
box.schema.role.exists('nosuchrole')
-- false for users
box.schema.role.exists('guest')
-- false for users
box.schema.role.exists('admin')
-- role id is ok
box.schema.role.exists(3)
-- user id 
box.schema.role.exists(0)
box.schema.role.create('public', { if_not_exists = true})
box.schema.user.create('admin', { if_not_exists = true})
box.schema.user.create('guest', { if_not_exists = true})
box.schema.user.create('test', { if_not_exists = true})
box.schema.user.create('test', { if_not_exists = true})
box.schema.role.drop('test', { if_not_exists = true})
box.schema.role.drop('test', { if_exists = true})
box.schema.role.create('test', { if_not_exists = true})
box.schema.role.create('test', { if_not_exists = true})
box.schema.user.drop('test', { if_not_exists = true})
-- gh-664 roles: accepting bad syntax for create
box.schema.role.create('role', 'role')
box.schema.role.drop('role', 'role')
box.schema.user.drop('test', { if_exists = true})

-- gh-663: inconsistent roles grant/revoke
box.schema.role.create('X1')
box.schema.role.create('X2')
box.schema.role.info('X1')
box.schema.role.grant('X1','read','role','X2')
box.schema.role.info('X1')
box.schema.role.revoke('X1','read','role','X2')
box.schema.role.info('X1')
box.schema.role.drop('X1')
box.schema.role.drop('X2')

-- gh-867 inconsistent role/user info
box.schema.role.create('test_role')
box.schema.role.info('test_role')
box.schema.user.info('test_role')
box.schema.role.info('test_not_exist')

box.schema.user.create('test_user')
box.schema.user.info('test_user')
box.schema.role.info('test_user')
box.schema.user.info('test_not_exist')

box.schema.role.drop('test_role')
box.schema.user.drop('test_user')

--gh-1266 if_exists for user drop
box.schema.user.create('test_1266')
box.schema.user.drop('test_1266')
box.schema.user.drop('test_1266')
box.schema.user.drop('test_1266', { if_exists = true})
