#!/bin/bash

# Fixed parameter
NODES=16
LINK_SPEED=100000
PATHS=16
# 100ms (100.000.000 ns) 
END_TIME=100000000
OUT_DIR="results/confronto_finale/tmp"
mkdir -p $OUT_DIR

# Array that contains all the dimension to test
SIZES=(4 16 64 256 1024 4096 16384 65536 262144 1048576 4194304 16777216 67108864 268435456)
LABELS=("4B" "16B" "64B" "256B" "1KiB" "4KiB" "16KiB" "64KiB" "256KiB" "1MiB" "4MiB" "16MiB" "64MiB" "256MiB")

for i in "${!SIZES[@]}"; do
    SIZE=${SIZES[$i]}
    LABEL=${LABELS[$i]}
    
    echo "==========================================="
    echo "TESTING SIZE: $LABEL ($SIZE bytes)"
    echo "==========================================="

    # 1. Matrix Generation
    python3 ../connection_matrices/gen_allreduce_bine.py $OUT_DIR/host_${LABEL}.cm $NODES $NODES $SIZE 42
    python3 ../connection_matrices/gen_allreduce_ina.py $OUT_DIR/ina_${LABEL}.cm $NODES $NODES $SIZE 42

    # 2. Host-based execution with GREP filtering
    echo "Running Host-based..."
    ../htsim_uec -tm $OUT_DIR/host_${LABEL}.cm -sender_cc_only -end $END_TIME \
    -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/host_${LABEL}.out

    # 3. INA execution with GREP filtering
    echo "Running In-Network..."
    ../htsim_uec -tm $OUT_DIR/ina_${LABEL}.cm -sender_cc_only -end $END_TIME \
    -linkspeed $LINK_SPEED -paths $PATHS -debug 2>&1 \
    | grep -E "Finished at|starting|Nodes|Connections|Done|New:|flowId" > $OUT_DIR/ina_${LABEL}.out

    echo "Done with $LABEL."
done

echo "All tests completed! Filtered files are in $OUT_DIR"
