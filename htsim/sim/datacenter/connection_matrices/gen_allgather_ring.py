# Generate a ring allgather traffic matrix.
# python gen_allgather_ring.py <nodes> <conns> <groupsize> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes
# <randseed>   Seed for random number generator, or set to 0 for random seed

import sys
from random import seed

if len(sys.argv) != 6:
    print("Usage: python gen_allgather_ring.py <filename> <nodes> <conns> <flowsize> <randseed>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])
randseed = int(sys.argv[5])


print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")
print("Random Seed ", randseed)

f = open(filename, "w")
print("Nodes", nodes, file=f)
print("Connections", conns*(conns-1), file=f)
print("Triggers", conns*(conns-2), file=f)


if randseed != 0:
    seed(randseed)

id = 0
trig_id = 1
for n in range(0,conns):
    for step in range(conns-1):
        id+=1
        src=(n+step)%conns
        dst=(n+step+1)%conns

        out = str(src) + "->" + str(dst) + " id " + str(id)

        if step == 0:
            out = out + " start 0"
        else:
            out = out + " trigger " + str(trig_id)
            trig_id += 1

        out = out + " size " + str(int(flowsize/conns))

        if step != conns-2:
            out = out + " send_done_trigger " + str(trig_id)
        print(out, file=f)
        print(src, "->", dst)

for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    print(out, file=f)

f.close()

