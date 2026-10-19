#!/usr/bin/env python

# Generate a Dragonfly+ "group-tornado" traffic matrix: the Dragonfly+ analog of the fat-tree tornado pattern

# USAGE: python gen_tornado_dfp.py <filename> <nodes> <conns> <flowsize> <extrastarttime> <randseed> <p> <l>

# <nodes>   number of nodes in the topology
# <conns>    number of active connections (<=nodes; a random subset of hosts sends)
# <flowsize>   size of the flows in bytes
# <extrastarttime>   How long in microseconds to space the start times over (start time will be random in between 0 and this time).  Can be a float.
# <randseed>   Seed for random number generator, or set to 0 for random seed
# <p>   hosts per leaf switch (Dragonfly+ topology parameter)
# <l>   leaf switches per group (Dragonfly+ topology parameter)

# p and l must match the Dragonfly+ topology this CM will be run against 

import os
import sys
from random import seed, shuffle

if len(sys.argv) != 9:
    print("Usage: python gen_tornado_dfp.py <filename> <nodes> <conns> <flowsize> "
          "<extrastarttime> <randseed> <p> <l>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])
extrastarttime = float(sys.argv[5])
randseed = int(sys.argv[6])
p = int(sys.argv[7])
l = int(sys.argv[8])

print("Nodes: ", nodes)
print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")
print("ExtraStartTime: ", extrastarttime, "us")
print("Random Seed ", randseed)
print("Dragonfly+ p=", p, "l=", l)

group_size = p * l
if group_size <= 0:
    print("p and l must both be positive")
    sys.exit(1)

no_groups = (nodes + group_size - 1) // group_size  # ceil(nodes / group_size)
if no_groups < 2:
    print(f"Only {no_groups} group(s) worth of hosts (group_size=p*l={group_size}, "
          f"nodes={nodes}) -- tornado needs at least 2 groups to have an "
          f"'opposite' group. Reduce p/l or increase nodes.")
    sys.exit(1)

offset = no_groups // 2
print(f"group_size={group_size}  no_groups={no_groups}  opposite group offset={offset}")

if randseed != 0:
    seed(randseed)

# Bucket host addresses by the group they fall in.
groups = [[] for _ in range(no_groups)]
for host in range(nodes):
    groups[host // group_size].append(host)

# Every host's destination is drawn from its opposite group, via a random permutation of that group's hosts
dsts = [None] * nodes
for g in range(no_groups):
    g_opp = (g + offset) % no_groups
    dst_pool = groups[g_opp][:]
    shuffle(dst_pool)
    if not dst_pool:
        continue
    for i, src in enumerate(groups[g]):
        dsts[src] = dst_pool[i % len(dst_pool)]

# Select which `conns` of the `nodes` hosts actually send.
active_srcs = list(range(nodes))
shuffle(active_srcs)
active_srcs = active_srcs[:conns]

f = open(filename, "w")
print("Nodes", nodes, file=f)
print("Connections", conns, file=f)
for n, src in enumerate(active_srcs):
    out = (str(src) + "->" + str(dsts[src]) + " id " + str(n + 1) + " start "
           + str(int(extrastarttime * 1000000)) + " size " + str(flowsize))
    print(out, file=f)
f.close()
