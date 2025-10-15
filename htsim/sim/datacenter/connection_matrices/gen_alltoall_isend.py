# Generate a isend-irecv all-to-all traffic matrix.
# python gen_alltoall_isend.py <nodes> <conns> <groupsize> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes
# <randseed>   Seed for random number generator, or set to 0 for random seed

import sys
from random import seed
import math

if len(sys.argv) != 6:
    print("Usage: python gen_alltoall_isend.py <filename> <nodes> <conns> <flowsize> <randseed>")
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

if randseed != 0:
    seed(randseed)


id = 0
trig_id = 1
for n in range(0,conns):
    src=n
    for step in range(conns-1):
        id+=1

        dst=(src+step+1)%conns

        out = str(src) + "->" + str(dst) + " id " + str(id)

        out = out + " start 0"
        
        out = out + " size " + str(int(flowsize/conns))

        print(out, file=f)
        print(src, "->", dst)


f.close()

