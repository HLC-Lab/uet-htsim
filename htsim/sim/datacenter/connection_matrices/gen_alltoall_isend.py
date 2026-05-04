# Generate a isend-irecv all-to-all traffic matrix.
# python gen_alltoall_isend.py <filename> <nodes> <conns> <flowsize> 
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes

import sys
import math

if len(sys.argv) != 5:
    print("Usage: python gen_alltoall_isend.py <filename> <nodes> <conns> <flowsize>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])


print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")

f = open(filename, "w")
print("Nodes", nodes, file=f)
print("Connections", conns*(conns-1), file=f)


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


f.close()

