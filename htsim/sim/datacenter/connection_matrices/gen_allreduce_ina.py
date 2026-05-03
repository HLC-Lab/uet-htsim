import sys
import math

if len(sys.argv) != 5:
    print("Usage: python gen_allreduce_ina.py <filename> <nodes> <conns> <flowsize>")
    sys.exit()

filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])

print("In-Network Aggregation Mode")
print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")

with open(filename, "w") as f:
    # Header: In this case we have 1 connection per participant and zero triggers, as the aggregation occurs in hardware in parallel.
    print(f"Nodes {nodes}", file=f)
    print(f"Connections {conns}", file=f)
    print(f"Triggers 0", file=f)

    # Flow generation toward the aggregator (#)
    for n in range(0, conns):
        src = n
        flow_id = n + 1
        # structure: src-># id X start 0 size Z
        out = f"{src}-># id {flow_id} start 0 size {flowsize}"
        print(out, file=f)
        
        # print(f"{src} -> #")
