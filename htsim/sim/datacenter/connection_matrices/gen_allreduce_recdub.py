# Generate a recoursive doubling allreduce traffic matrix.
# python gen_allreduce_recdub.py <filename> <nodes> <conns> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes

import sys
import math

if len(sys.argv) != 5:
    print("Usage: python gen_allreduce_recdub.py <filename> <nodes> <conns> <flowsize>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])


print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")

f = open(filename, "w")
print("Nodes", nodes, file=f)
print("Connections", conns*math.floor(math.log2(conns)), file=f)
print("Triggers", conns*(math.floor(math.log2(conns))-1), file=f)

id = 0
trig_id = 1
for n in range(0,conns):
    src=n
    for step in range(int(math.log2(conns))):
        dist=2**step
        id+=1

        if step!=0:
            src=dst

        if math.floor(src/dist)%2==0:
            dst=src+dist
        else:
            dst=src-dist

        out = str(src) + "->" + str(dst) + " id " + str(id)

        if step == 0:
            out = out + " start 0"
        else:
            out = out + " trigger " + str(trig_id)
            trig_id += 1

        out = out + " size " + str(flowsize)

        if step != int(math.log2(conns))-1:
            out = out + " send_done_trigger " + str(trig_id)
        print(out, file=f)

for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    print(out, file=f)

f.close()