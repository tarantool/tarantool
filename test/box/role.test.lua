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
