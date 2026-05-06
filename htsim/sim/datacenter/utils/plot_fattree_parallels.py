import sys
import re
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")  # non-interactive backend, also works on server/SSH
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

# Examples of lines we want to capture:
#   Flow Uec_0_1 flowId 1 uecSrc 0 starting at 0
#   Flow Uec_0_1 flowId 1 uecSrc 0 finished at 2.0064 ...

RE_FLOW_START = re.compile(
    r"Flow\s+Uec_(\d+)_(\d+)\s+flowId\s+(\d+).*?starting at\s+([0-9.]+)"
)

RE_FLOW_FINISH = re.compile(
    r"Flow\s+Uec_(\d+)_(\d+)\s+flowId\s+(\d+).*?finished at\s+([0-9.]+)"
)


def parse_log_by_flow(filepath):
    """
    Return:
        flows = {
            flow_id: (src, dst, t_start, t_end),
            ...
        }
    matching 'starting at' and 'finished at' lines by flowId.
    """
    ongoing = {}   # flowId -> (src, dst, t_start)
    flows = {}     # flowId -> (src, dst, t_start, t_end)

    with open(filepath, "r") as f:
        for line in f:
            m_start = RE_FLOW_START.search(line)
            if m_start:
                src = m_start.group(1)
                dst = m_start.group(2)
                flow_id = int(m_start.group(3))
                t_start = float(m_start.group(4))
                ongoing[flow_id] = (src, dst, t_start)
                continue

            m_finish = RE_FLOW_FINISH.search(line)
            if m_finish:
                src = m_finish.group(1)
                dst = m_finish.group(2)
                flow_id = int(m_finish.group(3))
                t_end = float(m_finish.group(4))

                if flow_id in ongoing:
                    src0, dst0, t_start = ongoing.pop(flow_id)
                    flows[flow_id] = (src0, dst0, t_start, t_end)

    return flows


def plot_flows_by_id(flows,
                     title="Utilization of flows in the Fat Tree without Scale Up (start–finish intervals, color per connection)"):
    """
    - X-axis: time (us)
    - Y-axis: flowId
    - Color: connection src -> dst
    """
    if not flows:
        print("No flow with start/finish found in the log.")
        return

    # Optimization for 56 flowIds
    flow_ids = sorted(flows.keys())

    # Ottimizzazione per 56 flowId
    y_spacing = 0.25      # vertical distance for flowId
    flow_to_y = {fid: i * y_spacing for i, fid in enumerate(flow_ids)}

    # I determine the connections and assign a color to each
    links = []
    for fid in flow_ids:
        src, dst, t_start, t_end = flows[fid]
        link = f"{src} -> {dst}"
        links.append(link)

    unique_links = sorted(set(links))
    colors = plt.cm.tab20
    link_to_color = {link: colors(i / len(unique_links)) for i, link in enumerate(unique_links)}


    # Figure
    # Taller graph to make it readable
    plt.figure(figsize=(14, 10 * 3))

    # I draw each flow as a colored horizontal segment
    for fid in flow_ids:
        src, dst, t_start, t_end = flows[fid]
        y = flow_to_y[fid]
        link = f"{src} -> {dst}"
        color = link_to_color[link]

        # flow duration bar
        plt.hlines(y, t_start, t_end, linewidth=7, color=color)

    # Y-axis labels: I put flowId (and optionally the link)
    y_values = [flow_to_y[fid] for fid in flow_ids]
    y_labels = [f"{fid} ({flows[fid][0]}->{flows[fid][1]})" for fid in flow_ids]

    plt.yticks(y_values, y_labels)

    plt.xlabel("Time (us)")
    plt.ylabel("flowId (color = connection)")
    plt.title(title)
    plt.grid(True, axis="x", linestyle="--", alpha=0.5)

    # Legend for links (colors)
    legend_elements = [
        Line2D([0], [0], color=link_to_color[link], lw=5, label=link)
        for link in unique_links
    ]
    plt.legend(
        handles=legend_elements,
        title="Connections",
        loc="upper right",
        bbox_to_anchor=(1.25, 1.0)
    )

    plt.tight_layout()

    outname = "fattree_flows_by_id.png"
    plt.savefig(outname, dpi=300, bbox_inches="tight")
    print(f"Chart saved in {outname}")


def main():
    if len(sys.argv) < 2:
        print("Uso: python plot_fattree_flows_by_id.py <file_log_simulation>")
        sys.exit(1)

    filepath = sys.argv[1]
    flows = parse_log_by_flow(filepath)
    plot_flows_by_id(flows)


if __name__ == "__main__":
    main()
