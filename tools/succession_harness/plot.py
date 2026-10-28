"""Line charts of the succession harness CSVs (cover per role; site state).

Usage: python3 plot.py <outdir>   (the directory the harness wrote)
Colours: validated categorical slots (orange, blue, aqua) with direct labels;
these are chart colours, not the sandbox's projected role colours.
"""
import csv, os, sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = sys.argv[1]
ROLES = [("pioneer", "#eb6834"), ("intermediate", "#2a78d6"), ("climax", "#1baf7a")]
INK, MUTED, GRID = "#2b2b29", "#6b6a64", "#e4e3de"


def load(name):
    with open(os.path.join(OUT, name + ".csv")) as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def style(ax, ylabel):
    ax.set_ylim(-0.02, 1.05)
    ax.set_ylabel(ylabel, color=MUTED)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.tick_params(colors=MUTED)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(GRID)


def label_end(ax, x, y, text, color):
    ax.plot(x[-1], y[-1], "o", color=color, markersize=4)
    ax.annotate(text, (x[-1], y[-1]), xytext=(6, 0), textcoords="offset points",
                va="center", color=INK, fontsize=9)


def cover_and_site(name, title, marks=()):
    d = load(name)
    t = d["t_s"]
    fig, (a, b) = plt.subplots(2, 1, figsize=(8, 5.6), sharex=True,
                               gridspec_kw={"height_ratios": [3, 2]})
    for key, col in ROLES:
        a.plot(t, d[key], color=col, linewidth=2)
        # label each role at its peak, so labels sit beside the curve that names them
        i = max(range(len(t)), key=lambda k: d[key][k])
        if d[key][i] > 0.05:
            a.annotate(key, (t[i], d[key][i]), xytext=(0, 6), textcoords="offset points",
                       ha="center", color=INK, fontsize=9)
    style(a, "cover (fraction of cell)")
    a.set_title(title, loc="left", color=INK, fontsize=11)
    for x, txt in marks:
        for ax in (a, b):
            ax.axvline(x, color=MUTED, linewidth=1, linestyle=(0, (3, 3)))
        a.annotate(txt, (x, 1.03), xytext=(3, 0), textcoords="offset points", color=MUTED, fontsize=8)
    b.plot(t, d["mean_soil"], color="#8a5a2b", linewidth=2)
    b.plot(t, d["mean_moisture"], color="#5b6f8c", linewidth=2)
    label_end(b, t, d["mean_soil"], "soil S", "#8a5a2b")
    label_end(b, t, d["mean_moisture"], "moisture M", "#5b6f8c")
    style(b, "site state (0-1)")
    b.set_xlabel("time (s)", color=MUTED)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name + "_chart.png"), dpi=110)
    plt.close(fig)


def t3():
    a, b = load("T3_patchA_secondary"), load("T3_patchB_primary")
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), sharey=True)
    for ax, d, ttl in ((axes[0], a, "Patch A: old soil (S x 0.6)"), (axes[1], b, "Patch B: new fill (S = 0)")):
        t = d["t_s"]
        for key, col in ROLES:
            ax.plot(t, d[key], color=col, linewidth=2)
            i = max(range(len(t)), key=lambda k: d[key][k])
            if d[key][i] > 0.05:
                ax.annotate(key, (t[i], d[key][i]), xytext=(0, 6), textcoords="offset points",
                            ha="center", color=INK, fontsize=9)
        style(ax, "cover" if ax is axes[0] else "")
        ax.set_title(ttl, loc="left", color=INK, fontsize=11)
        ax.set_xlabel("time since disturbance (s)", color=MUTED)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "T3_chart.png"), dpi=110)
    plt.close(fig)


cover_and_site("T1_primary_sequence", "T1: primary sequence on moist flat ground from S = 0 (designer preset)")
cover_and_site("T2_no_feedback", "T2: same, SOIL_BUILD_RATE = 0 (no feedback)")
cover_and_site("T4_moisture_memory", "T4: water on a dry strip from 10 to 40 s", [(10, "water on"), (40, "water off")])
cover_and_site("T5_alt_soil_preset", "T5: alt_soil preset (climax needs S >= 0.9)")
t3()
print("charts written to", OUT)
