#!/usr/bin/env python3
import subprocess
import sys


def size_to_bytes(size_str):
    """Convert size string with units to bytes."""
    size_str = size_str.strip().upper()
    if size_str.endswith("KIB"):
        return int(size_str[:-3]) * 1024
    elif size_str.endswith("MIB"):
        return int(size_str[:-3]) * 1024 * 1024
    elif size_str.endswith("GIB"):
        return int(size_str[:-3]) * 1024 * 1024 * 1024
    elif size_str.endswith("B"):
        return int(size_str[:-1])
    else:
        raise ValueError(f"Unknown size format: {size_str}")

''' 
collective :
  allgather: ['bine', 'bruck', 'recdub', 'ring']
  allreduce: ['bine', 'recdub']
  reducescatter: ['bine', 'recdub', 'rechalv', 'pairwise']
  alltoall: [ 'bruck', 'pairwise']
'''

if len(sys.argv) != 3:
    print("Usage: python3 run_simulation.py <collective> <algorithm>")
    sys.exit()
coll = sys.argv[1]
alg = sys.argv[2]

mess_size = ["2KiB", "16KiB", "128KiB", "1MiB", "8MiB", "64MiB"]

final_times_ft = []
final_times_df = []

times_ft = {ms: [] for ms in mess_size}
times_df = {ms: [] for ms in mess_size}

nodes_dfp = 8
nodes_ft = 8
num_runs = 1

for ms in mess_size:
    cmd = "../htsim/sim/build/datacenter/htsim_roce_dfp -nodes " + str(nodes_dfp) + " -strat minimal -end 100000000 -q 364 -linkspeed 400000 -hop_latency 0.02 -switch_latency 0.25"
    # Generate communication schedules before running simulations
    gen_cmd = f"python3 ../htsim/sim/datacenter/connection_matrices/gen_{coll}_{alg}.py dfp.cm {nodes_dfp} {nodes_dfp} {size_to_bytes(ms)} 0 > /dev/null"
    subprocess.run(gen_cmd, shell=True, check=True)
    cmd = cmd + " -tm dfp.cm"
    avg = 0
    for i in range(0, num_runs):        
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)

            output_lines = result.stdout.strip().split('\n')
            last_3_lines = output_lines[-3:] if len(output_lines) >= 5 else output_lines

            last_string = last_3_lines[0].split()
            t = float(last_string[last_string.index("at") + 1])

            avg += t
            times_df[ms].append(t)

        except subprocess.CalledProcessError as e:
            print(f"[Error during execution of test {i}]: {e}")
            error_output = e.stdout.strip().split('\n')[-5:] if e.stdout else ["[No output]"]
            times_df[ms].append(error_output)
    print(f"DFP {ms} {avg/num_runs}")

for ms in mess_size:
    cmd = "../htsim/sim/build/datacenter/htsim_roce_ft -nodes " + str(nodes_ft) + " -strat ecmp_host -paths 100 -end 100000000 -q 364 -linkspeed 400000 -hop_latency 0.02 -switch_latency 0.25"
    if nodes_ft < 256:
        cmd += " -tiers 2 "

    # Generate communication schedules before running simulations
    gen_cmd = f"python3 ../htsim/sim/datacenter/connection_matrices/gen_{coll}_{alg}.py ft.cm {nodes_ft} {nodes_ft} {size_to_bytes(ms)} 0 > /dev/null"
    subprocess.run(gen_cmd, shell=True, check=True)
    cmd = cmd + " -tm ft.cm"
    avg = 0     
    for i in range(0, num_runs):           
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)

            output_lines = result.stdout.strip().split('\n')
            last_3_lines = output_lines[-3:] if len(output_lines) >= 5 else output_lines

            last_string = last_3_lines[0].split()
            t = float(last_string[last_string.index("at") + 1])

            avg += t
            times_ft[ms].append(t)

        except subprocess.CalledProcessError as e:
            print(f"[Error during execution of test {i}]: {e}")
            error_output = e.stdout.strip().split('\n')[-5:] if e.stdout else ["[No output]"]
            times_ft[ms].append(error_output)
    
    print(f"FT {ms} {avg/num_runs}")
