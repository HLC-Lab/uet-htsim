import os
import re
import matplotlib.pyplot as plt
import numpy as np

# =========================================================
# 1. CONFIGURAZIONE
# =========================================================
OUT_DIR = "results/confronto_finale/tmp"

# Nuove dimensioni: da 4 Byte a 256 MiB (moltiplicando per 4)
SIZES_BYTES = [4, 16, 64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216, 67108864, 268435456]
LABELS = ["4B", "16B", "64B", "256B", "1KiB", "4KiB", "16KiB", "64KiB", "256KiB", "1MiB", "4MiB", "16MiB", "64MiB", "256MiB"]

# =========================================================
# 2. FUNZIONI DI ESTRAZIONE DATI
# =========================================================
def get_last_finished_time(filepath):
    """Legge il file riga per riga e trova il tempo massimo dell'evento 'finished at'."""
    max_time = 0.0
    pattern = r"finished at ([\d\.]+)"
    
    if not os.path.exists(filepath):
        print(f"   [ATTENZIONE] File non trovato: {filepath}")
        return None
    
    with open(filepath, 'r') as f:
        for line in f:
            match = re.search(pattern, line)
            if match:
                t = float(match.group(1))
                if t > max_time:
                    max_time = t
    return max_time if max_time > 0 else None

# Liste per salvare i risultati estratti
valid_sizes_bytes = []
valid_labels = []
time_host_us = []
time_inc_us = []

print("Estrazione dati dai file di log in corso...")

for label, size_b in zip(LABELS, SIZES_BYTES):
    host_file = os.path.join(OUT_DIR, f"host_{label}.out")
    inc_file = os.path.join(OUT_DIR, f"ina_{label}.out")
    
    t_host = get_last_finished_time(host_file)
    t_inc  = get_last_finished_time(inc_file)
    
    if t_host is not None and t_inc is not None and t_host > 0 and t_inc > 0:
        valid_sizes_bytes.append(size_b)      # Non dividiamo più per 1024*1024!
        valid_labels.append(label)            # Salviamo la stringa "4B", "1MiB", ecc.
        time_host_us.append(t_host)
        time_inc_us.append(t_inc)
        print(f"  [{label}] Host-based: {t_host} us | In-Network: {t_inc} us")
    else:
        print(f"  [{label}] Dati mancanti, salto...")

# Convertiamo in array numpy per comodità di calcolo
valid_sizes_bytes = np.array(valid_sizes_bytes)
time_host_us = np.array(time_host_us)
time_inc_us = np.array(time_inc_us)

# =========================================================
# 3. CALCOLO DELLA LARGHEZZA DI BANDA EFFETTIVA (GOODPUT)
# =========================================================
# Goodput calcolato come (Totale Bit del Vettore) / (Tempo di Completamento)
bits_sent = valid_sizes_bytes * 8

# Banda in Gbps
bw_host_gbps = (bits_sent / (time_host_us * 1e-6)) / 1e9
bw_inc_gbps = (bits_sent / (time_inc_us * 1e-6)) / 1e9

# =========================================================
# 4. GENERAZIONE DEL GRAFICO (Tempo e Banda)
# =========================================================
plt.style.use('seaborn-v0_8-whitegrid')
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))

# ---- GRAFICO 1: TEMPO DI COMPLETAMENTO ----
ax1.plot(valid_sizes_bytes, time_host_us / 1000, marker='o', markersize=6, linestyle='-', color='#1f77b4', linewidth=2.5, label='Host-based')
ax1.plot(valid_sizes_bytes, time_inc_us / 1000, marker='s', markersize=6, linestyle='--', color='#d62728', linewidth=2.5, label='In-Network')

ax1.set_xlabel('Dimensione del Vettore', fontsize=12)
ax1.set_ylabel('Flow Completion Time (us)', fontsize=12)
ax1.set_title('Confronto Tempi di Completamento (FCT)', fontsize=14, fontweight='bold')
ax1.set_xscale('log', base=2) 
ax1.set_yscale('log', base=10) 
ax1.set_xticks(valid_sizes_bytes)
ax1.set_xticklabels(valid_labels, rotation=45, ha='right') # Usa le etichette come "4B", "16KiB"
ax1.legend(fontsize=11)
ax1.grid(True, which="both", ls="--", alpha=0.6)

# ---- GRAFICO 2: LARGHEZZA DI BANDA (GOODPUT) ----
ax2.plot(valid_sizes_bytes, bw_host_gbps, marker='o', markersize=6, linestyle='-', color='#1f77b4', linewidth=2.5, label='Host-based')
ax2.plot(valid_sizes_bytes, bw_inc_gbps, marker='s', markersize=6, linestyle='--', color='#d62728', linewidth=2.5, label='In-Network')

ax2.set_xlabel('Dimensione del Vettore', fontsize=12)
ax2.set_ylabel('Goodput Algoritmico (Gbps)', fontsize=12)
ax2.set_title('Confronto Efficienza di Rete (Goodput)', fontsize=14, fontweight='bold')
ax2.set_xscale('log', base=2)
ax2.set_xticks(valid_sizes_bytes)
ax2.set_xticklabels(valid_labels, rotation=45, ha='right') # Usa le etichette come "4B", "16KiB"
ax2.legend(fontsize=11)
ax2.grid(True, which="both", ls="--", alpha=0.6)

# =========================================================
# 5. SALVATAGGIO IMMAGINE
# =========================================================
plt.tight_layout()
plt.savefig('grafici_tesi_risultati.png', dpi=300, bbox_inches='tight')
print("\nSuccesso! L'immagine è stata generata: 'grafici_tesi_risultati.png'")
plt.show()