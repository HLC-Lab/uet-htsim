import matplotlib.pyplot as plt
import math
from mpl_toolkits.axes_grid1.inset_locator import inset_axes, mark_inset

def genera_grafico_multi(dim_pacchetti, tempi_list, labels=None):
    """
    Main linear graph with an inset zoom for small message sizes.
    """

    epsilon = 1e-3

    for tempi in tempi_list:
        if len(tempi) != len(dim_pacchetti):
            raise ValueError("Each time list must have the same length as the packet size list.")

    if labels is None:
        labels = [f"Serie {i+1}" for i in range(len(tempi_list))]

    fig, ax = plt.subplots(figsize=(12, 6), constrained_layout=True)

    # -------------------------
    # Main graph
    # -------------------------
    for tempi, label in zip(tempi_list, labels):
        ax.plot(dim_pacchetti, tempi, marker='o', label=label)

    ax.set_title("Plot AllGather Ring")
    ax.set_xlabel("Dimensione Vettore")
    ax.set_ylabel("Tempo (ms)")

    ax.set_xscale("log", base=2)
    ax.grid(True, which="both", linestyle="--", alpha=0.7)

    labels_readable = [human_readable_size(x) for x in dim_pacchetti]
    ax.set_xticks(dim_pacchetti)
    ax.set_xticklabels(labels_readable, rotation=45)

    ax.legend(loc='upper left')

    # -------------------------
    # Inset: zoom on the initial range up to 8 MiB
    # -------------------------
    axins = inset_axes(
        ax,
        width="65%",
        height="48%",
        loc="upper left",
        bbox_to_anchor=(0.32, 0.08, 0.6, 0.85),
        bbox_transform=ax.transAxes,
        borderpad=0
    )

    epsilon_zoom = 100   # fake zero value used for zoom visualization

    for tempi, _label in zip(tempi_list, labels):
        tempi_safe = [t if t > 0 else epsilon_zoom for t in tempi]
        axins.plot(dim_pacchetti, tempi_safe, marker='o')

    axins.set_xscale("log", base=2)
    axins.set_yscale("log", base=10)

    # Zoom up to 8 MiB
    x_zoom = dim_pacchetti[:8]
    axins.set_xlim(x_zoom[0] * 0.8, x_zoom[-1] * 1.2)

    # Y-axis range for the zoomed inset
    axins.set_ylim(80, 1500)

    axins.set_xticks(x_zoom)
    axins.set_xticklabels(
        [human_readable_size(x) for x in x_zoom],
        rotation=45,
        fontsize=8
    )

    # Display tick value 100 as "0"
    axins.set_yticks([100, 500, 1000])
    axins.set_yticklabels(["0", "500", "1000"], fontsize=8)

    axins.grid(True, which="both", linestyle="--", alpha=0.5)

    mark_inset(ax, axins, loc1=2, loc2=4, fc="none", ec="0.5")

    plt.savefig("grafico_multi_misto.png", dpi=300, bbox_inches="tight")
    print("Graph saved as grafico_multi_misto.png")


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
    dim_pacchetti = [4, 32, 256, 2*1024, 16*1024, 128*1024, 1*1024*1024, 8*1024*1024, 64*1024*1024, 512*1024*1024]

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