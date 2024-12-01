-- Tarantool 2.11.5-entrypoint-103-ga2cc2b0080. Before release of the 2.11.5.
box.cfg{
    replicaset_uuid = 'b5c6e102-aa65-4b5f-a967-ee2a4f5d1480',
    instance_uuid = '22188ab6-e0e5-484c-89b6-5dfe2b969b7b',
    replication = {3301, 3302},
    listen = 3301,
}

box.schema.user.grant('guest', 'super')
while not box.info.replication[2] or
      not box.info.replication[2].downstream or
      box.info.replication[2].downstream.status ~= 'follow' do
    require('fiber').yield(0.1)
end
box.snapshot()
os.exit(0)
