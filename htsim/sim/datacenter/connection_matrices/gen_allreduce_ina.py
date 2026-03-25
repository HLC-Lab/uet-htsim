import sys
from random import seed
import math

# Utilizziamo la stessa firma di gen_allreduce_bine per coerenza
if len(sys.argv) != 6:
    print("Usage: python gen_allreduce_ina.py <filename> <nodes> <conns> <flowsize> <randseed>")
    sys.exit()

filename = sys.argv[1]
nodes = int(sys.argv[2])
conns = int(sys.argv[3])
flowsize = int(sys.argv[4])
randseed = int(sys.argv[5])

print("In-Network Aggregation Mode")
print("Connections: ", conns)
print("Flowsize: ", flowsize, "bytes")
print("Random Seed: ", randseed)

# Inizializzazione del seed identica a gen_allreduce_bine
if randseed != 0:
    seed(randseed)

with open(filename, "w") as f:
    # Header: In questo caso abbiamo 1 connessione per ogni partecipante
    # e 0 trigger perché l'aggregazione avviene in hardware parallelamente
    print(f"Nodes {nodes}", file=f)
    print(f"Connections {conns}", file=f)
    print(f"Triggers 0", file=f)

    # Generazione flussi verso l'aggregatore (#)
    for n in range(0, conns):
        src = n
        flow_id = n + 1
        # Struttura: src-># id X start 0 size Z
        out = f"{src}-># id {flow_id} start 0 size {flowsize}"
        print(out, file=f)
        
        # print(f"{src} -> #")