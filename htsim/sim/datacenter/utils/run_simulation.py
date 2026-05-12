import subprocess
import sys

''' 
collective :
  allgather: ['bine', 'bruck', 'recdub', 'ring']
  allreduce: ['bine', 'recdub']
  reducescatter: ['bine', 'recdub', 'rechalv', 'pairwise']
  alltoall: [ 'bruck', 'pairwise']
'''

if len(sys.argv) != 3:
    sys.exit()
coll = sys.argv[1]
alg = sys.argv[2]

mess_size = ["4B", "32B", "256B", "2KiB", "16KiB", "128KiB", "1MiB", "8MiB", "64MiB", "512MiB"]

final_times_ft = []
final_times_df = []

times_ft = {ms: [] for ms in mess_size}
times_df = {ms: [] for ms in mess_size}
print("Working on collective:", coll, " with algorithm:", alg)
'''
cmd = "./htsim_uec_mg"
for ms in mess_size:
    cmd = cmd + " -tm connection_matrices/" + coll + "_" + alg +"/" + coll + ms + ".cm -topo topologies/fat_tree_test_mg.topo -end 10000000"
    for i in range(1,4):
        print(f"\n==> Mg collective {coll} {alg} for message size {ms} test {i} ")
        
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)

            output_lines = result.stdout.strip().split('\n')
            last_3_lines = output_lines[-3:] if len(output_lines) >= 5 else output_lines

            last_string = last_3_lines[0].split()
            t = float(last_string[last_string.index("at") + 1])

            print(t)
            times_df[ms].append(t)

        except subprocess.CalledProcessError as e:
            print(f"[Error during execution of test {i}]: {e}")
            error_output = e.stdout.strip().split('\n')[-5:] if e.stdout else ["[No output]"]
            times_df[ms].append(error_output)
    cmd = "./htsim_uec_mg"
'''

cmd = "./htsim_uec_sh_mp"
for ms in mess_size:
    cmd = cmd + " -tm connection_matrices/" + coll + "_" + alg +"/" + coll + ms + ".cm -topo topologies/fat_tree_test_sh.topo -end 10000000"
    for i in range(1,4):
        print(f"\n==> Sh Mp collective {coll} {alg} for message size {ms} test {i} ")
        
        try:
            result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=True)

            output_lines = result.stdout.strip().split('\n')
            last_3_lines = output_lines[-3:] if len(output_lines) >= 5 else output_lines

            last_string = last_3_lines[0].split()
            t = float(last_string[last_string.index("at") + 1])

            print(t)
            times_ft[ms].append(t)

        except subprocess.CalledProcessError as e:
            print(f"[Error during execution of test {i}]: {e}")
            error_output = e.stdout.strip().split('\n')[-5:] if e.stdout else ["[No output]"]
            times_ft[ms].append(error_output)
    
    cmd = "./htsim_uec_sh_mp"

'''
for ms, time in times_df.items():
    avg = sum(time) / len(time)
    final_times_df.append(round(avg, 4))
'''

for ms, time in times_ft.items():
    avg = sum(time) / len(time)
    final_times_ft.append(round(avg, 4))


#print(f"\n==> Mg Tree {final_times_df}")
print(f"\n==> Sh Tree {final_times_ft}")



