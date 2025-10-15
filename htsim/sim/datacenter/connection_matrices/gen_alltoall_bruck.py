# Generate a bruck all-to-all traffic matrix.
# python gen_alltoall_bruck.py <nodes> <conns> <groupsize> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes
# <randseed>   Seed for random number generator, or set to 0 for random seed

import sys
from random import seed
import math

if len(sys.argv) != 6:
    print("Usage: python gen_alltoall_bruck.py <filename> <nodes> <conns> <flowsize> <randseed>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])
randseed = int(sys.argv[5])

power_2=False
if conns > 0 and (conns & (conns - 1)) == 0:
    power_2=True

print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")
print("Random Seed ", randseed)

f = open(filename, "w")
print("Nodes", nodes, file=f)
if power_2:
    print("Connections", conns*math.floor(math.log2(conns)), file=f)
    print("Triggers", conns*(math.floor(math.log2(conns))-1), file=f)
else:
    print("Connections", conns*(math.floor(math.log2(conns))+1), file=f)
    print("Triggers", conns*(math.floor(math.log2(conns))), file=f)


if randseed != 0:
    seed(randseed)

if power_2:
    max_step=int(math.log2(conns))
else:
    max_step=int(math.log2(conns))+1

id = 0
trig_id = 1
for n in range(0,conns):
    src=n
    for step in range(max_step):
        id+=1

        if step!=0:
            src=dst

        dst=(src+(2**step))%conns

        out = str(src) + "->" + str(dst) + " id " + str(id)

        if step == 0:
            out = out + " start 0"
        else:
            out = out + " trigger " + str(trig_id)
            trig_id += 1

        n_flows = 0
        for i in range(conns):
            if (i & (1 << step)) != 0:
                n_flows+=1
        
        out = out + " size " + str(int((flowsize/conns)*n_flows))


        if step != max_step-1:
            out = out + " send_done_trigger " + str(trig_id)
        print(out, file=f)
        print(src, "->", dst)

for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    print(out, file=f)

f.close()

