#!/bin/bash

# ==========================================
# Parametri fissi di simulazione
# ==========================================
NODES=16
CONNS=16
LINK_SPEED=100000
PATHS=16
# 100ms (100.000.000 ns) per dare tempo anche ai 256MiB di finire
END_TIME=100000000
OUT_DIR="results/test_allgather/tmp"

# Crea la directory se non esiste
mkdir -p $OUT_DIR

# ==========================================
# Array delle dimensioni da testare
# ==========================================
SIZES=(4 16 64 256 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864 268435456)
LABELS=("4B" "16B" "64B" "256B" "1KiB" "4KiB" "16KiB" "64KiB" "256KiB" "1MiB" "4MiB" "16MiB" "64MiB" "256MiB")

echo "Inizio batteria di test All-Gather BINE..."

for i in "${!SIZES[@]}"; do
    SIZE=${SIZES[$i]}
    LABEL=${LABELS[$i]}
    
    echo "==========================================="
    echo "TESTING SIZE: $LABEL ($SIZE bytes)"
    echo "==========================================="

    # 1. Generazione Matrice All-Gather Host-based (BINE)
    # Parametri passati: <filename> <nodes> <conns> <flowsize> <randseed>
    python3 ../connection_matrices/gen_allgather_bine.py $OUT_DIR/allgather_host_${LABEL}.cm $NODES $CONNS $SIZE 42
    
    # [OPZIONALE] - Se hai anche il generatore In-Network per l'All-Gather, decommenta qui:
    # python3 ../connection_matrices/gen_allgather_ina.py $OUT_DIR/allgather_ina_${LABEL}.cm $NODES $CONNS $SIZE 42

    # 2. Esecuzione Host-based con filtraggio GREP
    echo "Running Host-based All-Gather..."
    ../htsim_uec -tm $OUT_DIR/allgather_host_${LABEL}.cm -sender_cc_only -end $END_TIME \
    -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/allgather_host_${LABEL}.out

    # [OPZIONALE] - Se hai l'In-Network, decommenta qui:
    # echo "Running In-Network All-Gather..."
    # ../htsim_uec -tm $OUT_DIR/allgather_ina_${LABEL}.cm -sender_cc_only -end $END_TIME \
    # -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    # | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/allgather_ina_${LABEL}.out

    echo "Done with $LABEL."
done

echo "==========================================="
echo "Tutti i test completati! I file filtrati sono in $OUT_DIR"