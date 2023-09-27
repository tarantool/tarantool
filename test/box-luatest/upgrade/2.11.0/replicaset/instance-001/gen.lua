box.cfg{
    replicaset_uuid = 'cbf06940-0790-498b-948d-042b62cf3d29',
    instance_uuid = '8a274925-a26d-47fc-9e1b-af88ce939412',
    replication = {3301, 3302},
    listen = 3301,
}

box.schema.user.grant('guest', 'super')
box.schema.create_space('test_space')
while not box.info.replication[2] or
      not box.info.replication[2].downstream or
      box.info.replication[2].downstream.status ~= 'follow' do
    require('fiber').yield(0.1)
end
box.snapshot()
os.exit(0)
