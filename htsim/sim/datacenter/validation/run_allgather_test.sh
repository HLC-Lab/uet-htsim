#!/bin/bash

# ==========================================
# Fixed Simulation Parameters
# ==========================================
NODES=16
CONNS=16
LINK_SPEED=100000
PATHS=16
# 100ms (100.000.000 ns) 
END_TIME=100000000
OUT_DIR="results/test_allgather/tmp"

# Create directory if it doesn't exist
mkdir -p $OUT_DIR

# ==========================================
# Array that contains all the dimension to test
# ==========================================
SIZES=(4 16 64 256 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864 268435456)
LABELS=("4B" "16B" "64B" "256B" "1KiB" "4KiB" "16KiB" "64KiB" "256KiB" "1MiB" "4MiB" "16MiB" "64MiB" "256MiB")

echo "Start test All-Gather BINE..."

for i in "${!SIZES[@]}"; do
    SIZE=${SIZES[$i]}
    LABEL=${LABELS[$i]}
    
    echo "==========================================="
    echo "TESTING SIZE: $LABEL ($SIZE bytes)"
    echo "==========================================="

    # 1. Generation Matrix All-Gather Host-based (BINE)
    python3 ../connection_matrices/gen_allgather_bine.py $OUT_DIR/allgather_host_${LABEL}.cm $NODES $CONNS $SIZE 42
    
    # [OP] - If you also have the In-Network generator for All-Gather, uncomment here:
    # python3 ../connection_matrices/gen_allgather_ina.py $OUT_DIR/allgather_ina_${LABEL}.cm $NODES $CONNS $SIZE 42

    # 2. Host-based execution with GREP filtering
    echo "Running Host-based All-Gather..."
    ../htsim_uec -tm $OUT_DIR/allgather_host_${LABEL}.cm -sender_cc_only -end $END_TIME \
    -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/allgather_host_${LABEL}.out

    # [OP] - If using In-Network, uncomment here:
    # echo "Running In-Network All-Gather..."
    # ../htsim_uec -tm $OUT_DIR/allgather_ina_${LABEL}.cm -sender_cc_only -end $END_TIME \
    # -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    # | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/allgather_ina_${LABEL}.out

    echo "Done with $LABEL."
done

echo "==========================================="
echo "All tests completed! Filtered files are in $OUT_DIR"
