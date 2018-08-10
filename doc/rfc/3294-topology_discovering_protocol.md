# Topology Discovering Protocol

* **Status**: In progress
* **Start date**: 25-04-2018
* **Authors**: Konstantin Belyavskiy @kbelyavs k.belyavskiy@tarantool.org, Georgy Kirichenko @georgy georgy@tarantool.org, Konstantin Osipov @kostja kostja@tarantool.org
* **Issues**: [#3294](https://github.com/tarantool/tarantool/issues/3294)

## Summary

Introduce a new space **_routing** to store topology - a directed graph of routes between master and connected replicas.
Each node is responsible to keep its current list of subscriptions in this table. For example, on subscribe a new node inserts new records to this table representing its current list of subscriptions. If connection to some peer is dropped, node should delete associated records (subscription to this node and all nodes subscribed through it if any). Each time a node is connected again, a table also should be updated. Thus, for each change in topology, each affected downstream node (replica) should update associated records in this table.
Every change in this table should trigger a specific logic, which is responsible for subscriptions and could issue a resubscribe request if, for example, a new node is available or shorter path is found. If there are more than one path available, then a path with shorter number of intermediate peers should be preferred.

This Draft covers following topics:
- Discovering and maintaining current network topology. Propose a protocol describing how individual peer can observe topology and defining each node responsibility.
- Selective Subscribe. Extend SUBSCRIBE command with a list of server UUIDs for which SUBSCRIBE should fetch changes. In a full mesh configuration, only download records originating from the immediate peer. Do not download the records from other peers twice.
- Implement trigger and subscription logic, maintaining subscriptions based on known current network topology.

## Background and motivation

Currently each Tarantool instance will download from all peers all records in their WAL except records with instance id equal to the self instance id. For example, in a full mesh of 3 replicas all record will be fetched twice. Instead, it could send a subscribe request to its peers with server ids which are not present in other subscribe requests.
In more complex case, if there is no direct connection between two nodes, to subscribe through intermediate peers we should know network topology. So the first task is to build a topology and then a subscription logic based on observed topology.

## Detailed design

Building such topology is possible based on following principles:
- Each node is required to notify all his downstream peers (replicas) in case of changes with his upstream subscription configuration. It could be done by add/update/delete records in **_routing** table.
- The connection with lesser count of intermediate nodes has the highest priority. Lets define the number of edges between two peers as a Depth. So if A has direct connection with B, then Depth is 1 and if A connected with C through B, then Depth is 2. So if direct path between two nodes exists then it should be used in downstream peer subscription.
- In case of equal Depth connections first wins. But if shorter path is found, then node first should reconnect and then notify downstream peers with updated paths.

**_routing** table details.

| Subscriber | Replication Source | Subscribed via | Depth   |
| :--------- | :----------------- | :------------- | :------ |
| UUID       | UUID               | UUID           | Integer |

So peer notifies his downstream peers (replicas) with updates in **_routing** table which replicates to peers.
For example, imagine that A has a downstream B and B has C (there is no direct path between A and C). In such case B will have only one record in his table: {B_UUID, A_UUID, A_UUID, 1}. Initially C will have two records:
1. {C_UUID, B_UUID, B_UUID, 1} - its own connection to B;
2. {B_UUID, A_UUID, A_UUID, 1} - the one replicated from B;
Now it sees opportunity to connect to A through B, so it will insert third record:
3. {C_UUID, A_UUID, B_UUID, 2}.

### List of changes

1. Extend IPROTO_SUBSCRIBE command with a list of server UUIDs for which SUBSCRIBE should fetch changes. Store this UUIDs within applier's internal data structure. By default issuing SUBSCRIBE with empty list what means no filtering at all.
2. Implement white-list filtering in relay. After processing SUBSCRIBE request, relay has a list of UUIDs. Extract associated peer ids and fill in a filter. By default transmit all records, unless SUBSCRIBE was done with at least one server UUID. In latter case drop all records except originating from replicas in this list.
3. After issuing REQUEST_VOTE to all peers, subscription logic knows a map of server UUIDs, their peers and their vclocks. For each reachable UUID select shortest path and assign UUIDs to direct peer through it this pass goes. Issue the subscribe requests. Notify downstream peers with new topology.
4. On connect/disconnect actions and rebalancing.
Every update in **_routing** table or connect/disconnect with direct upstream peers should trigger logic which may start reassigning process.
 - On disconnect from direct master, a peer should first remove associated records from his table and replicates it to his downstreams, then reassigned all reachable UUIDs he was subscribed via this peer to other direct peers using shortest path rule. After successful connection notify downstream peers again by inserting new records to table.
 - On connect, by iterating through appliers list, find UUIDs with shorter path found, reassign them to correct peers and issue SUBSCRIBE for recently connected applier and for the one from whom we get these UUIDs back.
 - On every change in table if this change affected this peer (either shorter path is found or connection to some peer through one of upstreams is no longer available) do the same action as for disconnect/connect from direct peer.

### Details and open questions

On connect (new client or the old one reconnects) two options are available:
1. SUBSCRIBE only to direct peer and wait for updates in **_routing** to initiate further subscriptions.
2. SUBSCRIBE without any UUIDs (that means subscribe to all). This is the default option to subscribe to older version peers.

## Rationale and alternatives

### Topology Discovering

Instead of **_routing** table updates, encoded _iproto_ messages could be used. In this case, on every change in peer upstream topology, it should send a Map of *{UUID: depth}* representing its current list of subscriptions to all downstream peers (excluding subset of subscriptions obtaining from this peer in master-master configuration).
But this way has several major drawbacks: first, we need to provide a new channel for communication between Tarantool instances and second, a new protocol with acknowledgement should be provided.

### On network configuration change

On network configuration change what first, to notify peers or try to resubscribe?
1. If the peer is a direct peer, then we have most recent information about this node based on connection status. If available subscribe and immediately notify downstream peers.
2. On disconnect, it's more complex since in a connected subset if some node is disconnected, others can try to reconnect to this dead node through other nodes, but they do decisions based on old information resulting to massive resubscribe request (node A thinks that it has connection to C through B, but B thinks that it is connected through A). So I think, first need to notify replicas, that connection is dropped, and if other path is available try to resubscribe and then notify all downstream again. Or need to think about some kind of acknowledgement since it could be based on outdated information. To avoid such loops, one of two technique could be used: a search for loops in directed graph algorithm or use of validated Subscriber (based on fact that direct peer receives heartbeat messages from master and subscription chain should ends with validated peer).
3. On shorter path found, first resubscribe, then notify downstream peers.
4. Balancing. It's possible to slightly extend topology with number of peers subscribed for balancing but does it really needed?
