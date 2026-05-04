# Generate a pairwise-exchange reduce-scatter traffic matrix.
# python gen_reducescatter_pairwise.py <filename> <nodes> <conns> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes

import sys
import math

if len(sys.argv) != 5:
    print("Usage: python gen_reducesctter_pairwise.py <filename> <nodes> <conns> <flowsize>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])

power_2=False
if conns > 0 and (conns & (conns - 1)) == 0:
    power_2=True

print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")

f = open(filename, "w")
print("Nodes", nodes, file=f)

print("Connections", conns*(conns-1), file=f)
print("Triggers", conns*(conns-2), file=f)

id = 0
trig_id = 1
for n in range(0,conns):
    src=n
    for step in range(conns-1):
        id+=1

        dst=(src+step+1)%conns

        out = str(src) + "->" + str(dst) + " id " + str(id)

        if step == 0:
            out = out + " start 0"
        else:
            out = out + " trigger " + str(trig_id)
            trig_id += 1
        
        out = out + " size " + str(int(flowsize/conns))

        if step != conns-2:
            out = out + " send_done_trigger " + str(trig_id)

        src = dst

        print(out, file=f)

for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    print(out, file=f)

f.close()

