import matplotlib.pyplot as plt
import math

def genera_grafico_multi(dim_pacchetti, tempi_list, labels=None):
    """
    Genera un grafico con più curve sullo stesso asse cartesiano.
    
    Parameters:
        dim_pacchetti (list): dimensione dei pacchetti
        tempi_list (list of lists): elenco di liste di tempi
        labels (list): etichette da mostrare nella legenda
    """

    # Controllo che ogni vettore di tempi abbia la stessa lunghezza
    for tempi in tempi_list:
        if len(tempi) != len(dim_pacchetti):
            raise ValueError("Ogni lista di tempi deve avere la stessa lunghezza della lista delle dimensioni.")

    plt.figure(figsize=(10, 6))

    # Etichette di default
    if labels is None:
        labels = [f"Serie {i+1}" for i in range(len(tempi_list))]

    # Matplotlib assegna automaticamente colori diversi
    for tempi, label in zip(tempi_list, labels):
        plt.plot(dim_pacchetti, tempi, marker='o', label=label)

    plt.title("Plot AllGather Bruck")
    plt.xlabel("Dimensione Vettore")
    plt.ylabel("Tempo (ms)")
    plt.grid(True)

    plt.xscale("log", base=2)  

    labels_readable = [human_readable_size(x) for x in dim_pacchetti]
    plt.xticks(dim_pacchetti, labels_readable, rotation=45)  

    # Legenda
    plt.legend(loc='upper left')

    plt.subplots_adjust(bottom=0.22)

    # Salva il grafico
    plt.savefig("grafico_multi.png")
    print("Grafico salvato in grafico_multi.png")


def human_readable_size(size_bytes):
    """Converte un valore in byte in formato human-readable (KiB, MiB, GiB)."""
    if size_bytes == 0:
        return "0 B"

    units = ["B", "KiB", "MiB", "GiB", "TiB"]
    power = int(math.log(size_bytes, 1024))
    power = min(power, len(units) - 1)
    value = size_bytes / (1024 ** power)

    if value.is_integer():
        value = int(value)
    else:
        value = round(value, 2)

    return f"{value} {units[power]}"


# ------------------------
# Esempio di utilizzo
# ------------------------
if __name__ == "__main__":
    dim_pacchetti = [4 , 32 , 256 , 2*1024 , 16*1024 , 128*1024 , 1*1024*1024 , 8*1024*1024 , 64*1024*1024 , 512*1024*1024]

    tempi1 = [0.0, 0.0, 671.51, 671.889, 675.05, 699.36, 827.642, 1440.57, 6198.85, 44214.7] 
    tempi2 = [0.0, 0.0, 479.628, 479.802, 481.218, 492.644, 602.679, 1207.75, 6445.98, 49133.7]
    tempi3 = [0.0, 0.0, 540.569, 540.631, 541.121, 545.046, 631.468, 1225.81, 5969.1, 43981.0]
    tempi4 = [0.0, 0.0, 524.45, 524.498, 524.886, 527.986, 612.093, 1205.6, 5949.22, 43960.6]
    tempi5 = [0.0, 0.0, 542.168, 542.402, 544.27, 559.215, 611.631, 727.716, 5617.95, 44567.6]

    genera_grafico_multi(
        dim_pacchetti,
        [tempi1, tempi2, tempi3, tempi4, tempi5],
        labels=["Fat Tree", "Multi Gpu", "Scale Up 32 Porte", "Scale Up 64 Porte", "Scale Up Multi Planes"]
    )
