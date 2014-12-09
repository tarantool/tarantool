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
box.schema.user.grant('iddqd', 'execute', 'role', 'iddqd')
box.schema.user.grant('iddqd', 'iddqd')
box.schema.user.revoke('iddqd', 'iddqd')
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
box.schema.user.grant('b', 'a')
box.schema.user.grant('c', 'a')
box.schema.user.grant('d', 'b')
box.schema.user.grant('d', 'c')
box.schema.user.grant('a', 'd')
box.schema.role.drop('d')
box.schema.role.drop('b')
box.schema.role.drop('c')
box.schema.role.drop('a')
-- check that when dropping a role, it's first revoked
-- from whoever it is granted
box.schema.role.create('a')
box.schema.role.create('b')
box.schema.user.grant('b', 'a')
box.schema.role.drop('a')
box.schema.user.info('b')
box.schema.role.drop('b')
-- check a grant received via a role
box.schema.user.create('test')
box.schema.user.create('grantee')
box.schema.role.create('liaison')
box.schema.user.grant('grantee', 'liaison')
box.schema.user.grant('test', 'read,write', 'universe')
box.session.su('test')
s = box.schema.space.create('test')
s:create_index('i1')
box.schema.role.grant('liaison', 'read,write', 'space', 'test')
box.session.su('grantee')
box.space.test:insert{1}
box.space.test:select{1}
box.session.su('test')
box.schema.user.revoke('liaison', 'read,write', 'space', 'test')
box.session.su('grantee')
box.space.test:insert{1}
box.space.test:select{1}
box.session.su('admin')
box.schema.user.drop('test')
box.schema.user.drop('grantee')
box.schema.user.drop('liaison')


-- cleanup
