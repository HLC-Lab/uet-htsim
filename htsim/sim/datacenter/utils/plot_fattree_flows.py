import sys
import re
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")  # non-interactive backend, works fine on server/SSH
import matplotlib.pyplot as plt

#   Regex for start/end flow:
#   Flow Uec_0_1 flowId 1 uecSrc 0 starting at 0
#   Flow Uec_0_1 flowId 1 uecSrc 0 finished at 2.0064 ...

RE_FLOW_START = re.compile(
    r"Flow\s+Uec_(\d+)_(\d+)\s+flowId\s+(\d+).*?starting at\s+([0-9.]+)"
)

RE_FLOW_FINISH = re.compile(
    r"Flow\s+Uec_(\d+)_(\d+)\s+flowId\s+(\d+).*?finished at\s+([0-9.]+)"
)


def parse_log_intervals(filepath):
    """
    Return:
        { "src -> dst": [(t_start, t_end), ...], ... }
    matching 'starting at' and 'finished at' lines by flowId.
    """
    link_intervals = defaultdict(list)
    ongoing = {}  # flowId -> (src, dst, t_start)

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
                    link = f"{src0} -> {dst0}"
                    link_intervals[link].append((t_start, t_end))

    return link_intervals


def plot_link_intervals(link_intervals,
                        title="Utilization of connections host↔host Fat Tree Without Scale Up (start–finish intervals)"):
    if not link_intervals:
        print("No start/finish interval found in the log.")
        return

    # Sorted Links (Y-Axis)
    links = sorted(link_intervals.keys())

    # Vertical spacing very compact
    y_spacing = 0.1       # if you want even more compressed, try 0.05
    y_values = [i * y_spacing for i in range(len(links))]
    link_to_y = {link: y for link, y in zip(links, y_values)}

    plt.figure(figsize=(12, 4))
    colors = plt.cm.tab20

    for i, (link, intervals) in enumerate(link_intervals.items()):
        y = link_to_y[link]
        color = colors(i % 20)

        for t_start, t_end in intervals:
            # Colored bar for link usage
            plt.hlines(y, t_start, t_end, linewidth=8, color=color)

            # Black marker at the start of the broadcast
            # small vertical segment centered on y
            marker_half_height = y_spacing * 0.3
            plt.vlines(
                t_start,
                y - marker_half_height,
                y + marker_half_height,
                linewidth=1.5,
                color="black"
            )

    plt.yticks(y_values, links)
    plt.xlabel("Time (us)")
    plt.ylabel("Connection host → host")
    plt.title(title)
    plt.grid(True, axis="x", linestyle="--", alpha=0.5)
    plt.tight_layout()

    outname = "fattree_usage_intervals.png"
    plt.savefig(outname, dpi=300)
    print(f"Chart saved in {outname}")



def main():
    if len(sys.argv) < 2:
        print("Usage: python plot_fattree_flows_intervals.py <file_log_simulation>")
        sys.exit(1)

    filepath = sys.argv[1]
    link_intervals = parse_log_intervals(filepath)
    plot_link_intervals(link_intervals)


if __name__ == "__main__":
    main()
