# Generate a recoursive halving reduce-scatter traffic matrix.
# python gen_reducescatter_rec_halv.py <nodes> <conns> <groupsize> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes
# <randseed>   Seed for random number generator, or set to 0 for random seed

import sys
from random import seed
import math

RHOS = [1, -1, 3, -5, 11, -21, 43, -85, 171, -341, 683, -1365, 2731, -5461, 10923, -21845, 43691, -87381, 174763, -349525]
def pi(rank, step, conns):
    if (rank & 1) == 0:
        dst = (rank + RHOS[step]) % conns
    else:
        dst = (rank - RHOS[step]) % conns

    if dst < 0:
        dst += conns

    return dst



if len(sys.argv) != 6:
    print("Usage: python gen_reducescatter_rec_halv.py <filename> <nodes> <conns> <flowsize> <randseed>")
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
print("Connections", conns*math.floor(math.log2(conns)), file=f)
print("Triggers", conns*(math.floor(math.log2(conns))-1), file=f)


if randseed != 0:
    seed(randseed)

id = 0
trig_id = 1
for n in range(0,conns):
    src=n
    for step in range(int(math.log2(conns))):
        id+=1

        if step!=0:
            src=dst

        dst = pi(src, step, conns)

        out = str(src) + "->" + str(dst) + " id " + str(id)

        if step == 0:
            out = out + " start 0"
        else:
            out = out + " trigger " + str(trig_id)
            trig_id += 1

        out = out + " size " + str(int(flowsize/(2**(step+1))))

        if step != int(math.log2(conns))-1:
            out = out + " send_done_trigger " + str(trig_id)
        print(out, file=f)
        print(src, "->", dst)

for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    print(out, file=f)

f.close()

