box.execute("CREATE TABLE supersecret(id INT PRIMARY KEY, data TEXT);")
box.execute("CREATE TABLE supersecret2(id INT PRIMARY KEY, data TEXT);")
box.execute("INSERT INTO supersecret VALUES(1, 'very very big secret');")
box.execute("INSERT INTO supersecret2 VALUES(1, 'very big secret 2');")
box.execute("CREATE VIEW supersecret_leak AS  SELECT * FROM supersecret, supersecret2;")
remote = require 'net.box'
cn = remote.connect(box.cfg.listen)

box.schema.user.grant('guest','read', 'space', 'SUPERSECRET_LEAK')
cn:execute('SELECT * FROM SUPERSECRET_LEAK')
box.schema.user.grant('guest','read', 'space', 'SUPERSECRET')
cn:execute('SELECT * FROM SUPERSECRET_LEAK')

box.schema.user.revoke('guest','read', 'space', 'SUPERSECRET')
box.schema.user.revoke('guest','read', 'space', 'SUPERSECRET_LEAK')
box.execute("DROP VIEW supersecret_leak")
box.execute("DROP TABLE supersecret")
box.execute("DROP TABLE supersecret2")
