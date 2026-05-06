# HTSim Utilities and Traffic Matrix Generators

This repository contains a collection of Python utilities developed to support experiments with the C++ network simulator **HTSim**.

The scripts are used to:

- generate traffic matrices (`.cm`) for different communication patterns and collective algorithms;
- automate repeated simulations;
- analyze HTSim log outputs;
- generate plots and visualizations of flow execution and collective performance.

The utilities are mainly focused on:

- collective communication workloads (`AllGather`, `AllReduce`, `AllToAll`);
- traffic patterns (`Permutation`, `Incast`, `Outcast-Incast`);
- Fat-Tree and Scale-Up topology experiments.

---

# Repository Structure

## Simulation Automation

| File | Description |
|---|---|
| `run_simulation.py` | Automates execution of HTSim simulations for different collective algorithms and message sizes. |

---

## Plotting and Log Analysis

| File | Description |
|---|---|
| `plot_fattree_flows.py` | Parses HTSim logs and plots flow lifetimes grouped by `flowId`. |
| `plot_fattree_parallels.py` | Visualizes parallel communication intervals between host pairs in Fat-Tree experiments. |
| `Graph.py` | Generic utility for plotting performance curves of multiple experiments. |
| `Graph_log.py` | Extended plotting utility with zoomed inset for better visualization of small-message behavior. |

---

# Traffic Matrix Generators

## AllGather Generators

| File | Algorithm |
|---|---|
| `gen_allgather_bine.py` | Binary-Exchange AllGather generator |
| `gen_allgather_bruck.py` | Bruck AllGather generator |
| `gen_allgather_recdub.py` | Recursive-Doubling AllGather generator |
| `gen_allgather_ring.py` | Ring-based AllGather generator |

---

## AllReduce Generators

| File | Algorithm |
|---|---|
| `gen_allreduce.py` | Ring-based AllReduce generator |
| `gen_allreduce_butterfly.py` | Butterfly-style AllReduce generator |

---

## AllToAll Generators

| File | Description |
|---|---|
| `gen_serial_alltoall.py` | Sequential AllToAll communication pattern |
| `gen_serialn_alltoall.py` | Parallelized serial AllToAll generator |
| `gen_serialn_alltoall_prio.py` | Parallelized AllToAll with flow priorities |

---

## Other Traffic Patterns

| File | Description |
|---|---|
| `gen_incast.py` | Generates incast traffic patterns |
| `gen_outcast_incast.py` | Generates mixed outcast + incast traffic patterns |
| `gen_permutation.py` | Generates random permutation traffic matrices |
| `gen_permutation_full_bisection.py` | Permutation generator enforcing full bisection bandwidth usage |

---

# Requirements

The scripts require:

- Python 3.8+
- `matplotlib`

Install dependencies with:

```bash
pip install matplotlib

Some scripts also rely on:

- `mpl_toolkits`
- standard Python libraries (`math`, `random`, `subprocess`, `collections`, `re`, `sys`, `os`)

---

# HTSim Integration

The scripts assume the presence of HTSim executables such as:

```bash
./htsim_uec_sh_mp
./htsim_uec_mg
```

and topology / connection matrix folders such as:

```text
topologies/
connection_matrices/
```

Typical simulation execution:

```bash
./htsim_uec_sh_mp \
    -tm connection_matrices/allgather_ring/allgather1MiB.cm \
    -topo topologies/fat_tree_test_sh.topo \
    -end 10000000
```

---

# Traffic Matrix Format

Generated `.cm` files follow the HTSim traffic matrix format:

```text
Nodes <N>
Connections <M>
Triggers <T>

<src>-><dst> id <id> start <time> size <bytes>
```

Some generators also use:

- `trigger`
- `send_done_trigger`
- `recv_done_trigger`
- `prio`

to model synchronized collective communication phases.

---

# Usage Examples

## 1. Generate an AllGather Ring Traffic Matrix

```bash
python gen_allgather_ring.py output.cm 64 64 1048576 1
```

Parameters:

```text
<filename> <nodes> <conns> <flowsize> <randseed>
```

Example:
- 64 nodes
- 1 MiB collective payload
- deterministic random seed

---

## 2. Generate an AllReduce Butterfly Matrix

```bash
python gen_allreduce_butterfly.py allreduce.cm 64 2 32 1048576 0 1
```

Parameters:

```text
<filename> <nodes> <groups> <groupsize> <flowsize> <locality> <randseed>
```

---

## 3. Run Automated Simulations

```bash
python run_simulation.py allgather ring
```

The script:
- iterates over multiple message sizes;
- launches HTSim simulations;
- extracts completion times from simulator logs;
- computes average execution times.

Supported collectives include:

```python
allgather
allreduce
reducescatter
alltoall
```

---

## 4. Plot Flow Execution Timelines

```bash
python plot_fattree_flows.py simulation.log
```

Output:

```text
fattree_flows_by_id.png
```

The graph shows:
- flow start/end intervals;
- execution overlap;
- communication concurrency.

---

## 5. Plot Host-to-Host Utilization

```bash
python plot_fattree_parallels.py simulation.log
```

Output:

```text
fattree_usage_intervals.png
```

Useful for:
- congestion analysis;
- link utilization inspection;
- communication parallelism visualization.

---

# Plotting Utilities

## `Graph.py`

Provides generic multi-series plotting for collective benchmarks.

Features:
- logarithmic X-axis;
- human-readable packet sizes;
- automatic legends;
- multi-topology comparisons.

---

## `Graph_log.py`

Enhanced plotting utility with:
- logarithmic inset zoom;
- improved visibility for small message sizes;
- mixed linear/logarithmic visualization.

---

# Notes

- Most generators assume powers-of-two group sizes for recursive algorithms.
- Some scripts internally shuffle node mappings to randomize traffic placement.
- Priority-based generators can emulate more realistic collective scheduling.
- Flow sizes are specified in **bytes**.
- Start times are generally expressed in **microseconds**.

---

# Suggested Directory Layout

```text
project/
│
├── connection_matrices/
├── topologies/
├── logs/
├── plots/
│
├── run_simulation.py
├── plot_fattree_flows.py
├── plot_fattree_parallels.py
│
├── gen_allgather_*.py
├── gen_allreduce*.py
├── gen_*traffic*.py
│
├── Graph.py
├── Graph_log.py
│
└── README.md
```

---

# Author Notes

These utilities were developed as experimental support tools for evaluating:
- collective communication algorithms;
- Fat-Tree and Scale-Up topologies;
- flow scheduling strategies;
- congestion and parallelism behavior in HTSim simulations.