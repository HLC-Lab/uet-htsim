import matplotlib.pyplot as plt
import math

def genera_grafico_multi(dim_pacchetti, tempi_list, labels=None):
    """
    Generate a graph with multiple curves on the same Cartesian plane.

    Parameters:
        dim_pacchetti (list): packet sizes
        tempi_list (list of lists): list of execution time series
        labels (list): labels shown in the legend
    """

    # Check that each time vector has the same length
    for tempi in tempi_list:
        if len(tempi) != len(dim_pacchetti):
            raise ValueError("Each time list must have the same length as the packet size list.")

    plt.figure(figsize=(10, 6))

    # Default labels
    if labels is None:
        labels = [f"Serie {i+1}" for i in range(len(tempi_list))]

    # Matplotlib automatically assigns different colors
    for tempi, label in zip(tempi_list, labels):
        plt.plot(dim_pacchetti, tempi, marker='o', label=label)

    plt.title("Plot AllGather Bruck")
    plt.xlabel("Dimensione Vettore")
    plt.ylabel("Tempo (ms)")
    plt.grid(True)

    plt.xscale("log", base=2)  

    labels_readable = [human_readable_size(x) for x in dim_pacchetti]
    plt.xticks(dim_pacchetti, labels_readable, rotation=45)  

    # Legend
    plt.legend(loc='upper left')

    plt.subplots_adjust(bottom=0.22)

    # Save the graph
    plt.savefig("grafico_multi.png")
    print("Graph saved as grafico_multi.png")


def human_readable_size(size_bytes):
    """Convert a byte value into a human-readable format (KiB, MiB, GiB)."""
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
# Example usage
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
