# Generate a bine allgather traffic matrix.
# python gen_allgather_bine.py <filename> <nodes> <conns> <flowsize>
# Parameters:
# <nodes>   number of nodes in the topology
# <conns>    number of active connections
# <flowsize>   size of the flows in bytes

import sys
import math

smallest_negabinary = [0, 0, -2, -2, -10, -10, -42, -42, -170, -170, -682, -682, -2730, -2730, -10922, -10922, -43690, -43690, -174762, -174762]
largest_negabinary = [0, 1, 1, 5, 5, 21, 21, 85, 85, 341, 341, 1365, 1365, 5461, 5461, 21845, 21845, 87381, 87381, 349525]

RHOS = [1, -1, 3, -5, 11, -21, 43, -85, 171, -341, 683, -1365, 2731, -5461, 10923, -21845, 43691, -87381, 174763, -349525]
def pi(rank, step, conns):
    if (rank & 1) == 0:
        dst = (rank + RHOS[step]) % conns
    else:
        dst = (rank - RHOS[step]) % conns

    if dst < 0:
        dst += conns

    return dst

def in_range(x, nbit):
    return x >= smallest_negabinary[nbit] and x <= largest_negabinary[nbit]

def negabinary_to_binary(x):
    mask = 0xAAAAAAAA
    return ((mask ^ x) - mask) & 0xFFFFFFFF

def binary_to_negabinary(x):
    mask = 0xAAAAAAAA
    return ((mask + x) ^ mask) & 0xFFFFFFFF

def bit_reverse_32(n):
    # Reverses bits of a 32-bit integer
    n = ((n >> 1) & 0x55555555) | ((n & 0x55555555) << 1)
    n = ((n >> 2) & 0x33333333) | ((n & 0x33333333) << 2)
    n = ((n >> 4) & 0x0F0F0F0F) | ((n & 0x0F0F0F0F) << 4)
    n = ((n >> 8) & 0x00FF00FF) | ((n & 0x00FF00FF) << 8)
    n = ((n >> 16) & 0x0000FFFF) | ((n & 0x0000FFFF) << 16)
    return n & 0xFFFFFFFF

def nb_to_nu(nb, size):
    return  bit_reverse_32(nb ^ (nb >> 1)) >> 32 - math.ceil(math.log2(size))

def get_nu(rank, conns):
    nba, nbb = (1 << 32) - 1, (1 << 32) - 1
    num_bits = math.ceil(math.log2(conns))
    if (rank % 2):
        if (in_range(rank, num_bits)):
            nba = binary_to_negabinary(rank)
        if (in_range(rank - conns, num_bits)):
            nbb = binary_to_negabinary(rank - conns)
    else:
        if (in_range(-rank, num_bits)):
            nba = binary_to_negabinary(-rank)
        if (in_range(-rank + conns, num_bits)):
            nbb = binary_to_negabinary(-rank + conns)
    if (nba == (1 << 32) - 1 and nbb == (1 << 32) - 1):
        # there is an error
        print("error")
    
    if (nba == (1 << 32) - 1 and nbb != (1 << 32) - 1):
        return nb_to_nu(nbb, conns)
    elif (nba != (1 << 32) - 1 and nbb == (1 << 32) - 1):
        return nb_to_nu(nba, conns)
    else:
        nu_a = nb_to_nu(nba, conns)
        nu_b = nb_to_nu(nbb, conns)
        if (nu_a < nu_b):
            return nu_a
        else:
            return nu_b

def clz32(n):
    if n == 0:
        return 32
    return 32 - n.bit_length()


if len(sys.argv) != 5:
    print("Usage: python gen_allgather_bine.py <filename> <nodes> <conns> <flowsize>")
    sys.exit()
filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])


print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")

lines_to_write = []

id = 0
trig_id = 1
num_conn = 0

for n in range(0,conns):
    src = n
    step = 0
    for mask in reversed(range(math.ceil(math.log2(conns)))):

        dst = pi(src, mask, conns)

        to_write_last_step = []

        for block in range(1, conns):

            k = 31 - clz32(get_nu(block, conns))

            if (k == step or block == 0):
                if (src % 2 == 0):
                    block_to_send = (dst - block) % conns
                else:
                    block_to_send = (block + dst) % conns

                
                if (block_to_send != dst):
                    id+=1

                    out = str(src) + "->" + str(dst) + " id " + str(id)

                    if step == 0:
                        out = out + " start 0 size " + str(int(flowsize/conns))
                    else:
                        if step != math.ceil(math.log2(conns))-1:
                            out = out + " trigger " + str(trig_id)
                            trig_id += 1

                            out = out + " size " + str(int(flowsize/conns))

                    if step != math.ceil(math.log2(conns))-1:
                        out = out + " send_done_trigger " + str(trig_id)
                        num_conn += 1
                        lines_to_write.append(out)
                    else: 
                        to_write_last_step.append(out)

        step += 1
        src = dst
    
    # to append all the last blocks
    if to_write_last_step:
        for i, out in enumerate(to_write_last_step):
            out = out + " trigger " + str(trig_id) + " size " + str(int(flowsize/conns))
            trig_id += 1
            if i != len(to_write_last_step) - 1:
                out = out + " send_done_trigger " + str(trig_id)
            num_conn += 1
            lines_to_write.append(out)



for t in range(1, trig_id):
    out = "trigger id " + str(t) + " oneshot"
    lines_to_write.append(out)

with open(filename, "w") as f:
    print(f"Nodes", nodes, file=f)
    print(f"Connections", num_conn, file=f) 
    print(f"Triggers", trig_id - 1, file=f)  
    
    for line in lines_to_write:
        print(line, file=f)



