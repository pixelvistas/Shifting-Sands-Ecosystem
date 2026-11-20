#!/usr/bin/env python3
"""Writes bin/data/lenia/species.json (branch prototype/6-lenia).

Every published preset is copied from its source file, not typed in:
- Lenia: Chan's Python/animals.json (github.com/Chakazul/Lenia), entries
  picked by code, name and R; params (R, T, b, m, s, kn, gn) and the RLE
  cells are copied verbatim.
- Flow-Lenia: the published code has no stored creatures; its examples
  draw a rule from a seeded PRNG. flowlenia_seeds.py reproduces those
  draws (jax.random, threefry) into a JSON file read here.
Tuned entries are written from the constants below and say so.

    python3 tools/lenia_lab/make_species.py <animals.json> <flowlenia_seeds.json> [search.json seed,seed,...]

search.json: results of the lab's random search over the published Flow-Lenia
rule space (tools/lenia_lab/flowlenia_search.py): [seed, score, info, params].
The chosen seeds are written as tuned species (one channel).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "bin", "data", "lenia", "species.json")

CHAN_REF = ("Chan B. W.-C. (2019) Lenia: Biology of Artificial Life. Complex Systems 28(3), 251-286; "
            "params and cells copied from github.com/Chakazul/Lenia Python/animals.json")
CHAN_COMMIT = "master as fetched 2026-11-19"
PLANTEC_REF = ("Plantec E., Hamon G., Etcheverry M., Oudeyer P.-Y., Moulin-Frier C., Chan B. W.-C. (2023) "
               "Flow-Lenia: Towards open-ended evolution in cellular automata through mass conservation "
               "and parameter localization. ALIFE 2023 (arXiv 2212.07906); code github.com/erwanplantec/FlowLenia")

# (code, name, R) -> id, note
CHAN_PICKS = [
    ("O2u", "Orbium unicaudatus", 13, "orbium", "The standard glider."),
    ("O2b", "Orbium bicaudatus", 13, "orbium_bicaudatus", "Two-tailed Orbium."),
    ("OG2g", "Gyrorbium gyrans", 13, "gyrorbium", "Glides on a curved path."),
    ("S1s", "Scutium solidus", 13, "scutium", "Shield shape, slower."),
    ("2S1f", "Scutium serratus fluens", 15, "scutium_serratus", "Two shells (b 1/2, 1)."),
    ("K4s", "Kronium solidus", 18, "kronium", "Two shells (b 1, 1/3)."),
    ("P4al", "Paraptera arcus labens", 13, "paraptera", "Larger, winged."),
    ("3GH2n", "Hydrogeminium natans", 18, "hydrogeminium", "Three shells; large."),
]


def frac_list(b):
    return b  # kept as Chan's string ("1/2,1"); LeniaSpecies parses fractions


def chan_species(animals):
    out = []
    for code, name, R, sid, note in CHAN_PICKS:
        hits = [a for a in animals if a.get("code") == code and a.get("name") == name
                and a.get("params", {}).get("R") == R]
        if not hits:
            raise SystemExit("not found in animals.json: %s %s R=%d" % (code, name, R))
        a = hits[0]
        p = a["params"]
        out.append({
            "id": sid,
            "name": "%s (%s)" % (a["name"], code),
            "status": "published",
            "source": "%s, code '%s', name '%s' (%s; first entry with R=%d)." % (CHAN_REF, code, name, CHAN_COMMIT, R),
            "notes": note + " kn/gn 1 = polynomial core and growth, as LeniaND.py computes them.",
            "model": "lenia",
            "R": p["R"],
            "T": p["T"],
            "kernels": [{"from": 0, "to": 0, "shape": "shells", "b": frac_list(p["b"]), "kn": p["kn"],
                         "m": p["m"], "s": p["s"], "h": 1, "gn": p["gn"]}],
            "cells": a["cells"] if isinstance(a["cells"], str) else "".join(a["cells"]),
        })
    return out


def rings_kernels(p, c0=None, c1=None):
    k = len(p["r"])
    to = {}
    if c1 is not None:
        for c, ks in enumerate(c1):
            for i in ks:
                to[i] = c
    ks = []
    for i in range(k):
        ks.append({"from": (c0[i] if c0 else 0), "to": to.get(i, 0), "shape": "rings",
                   "r": p["r"][i], "a": p["a"][i], "b": p["b"][i], "w": p["w"][i],
                   "m": p["m"][i], "s": p["s"][i], "h": p.get("h", [1.0] * k)[i], "gn": 2})
    return ks


def search_species(results, chosen):
    out = []
    for seed in chosen:
        hit = [r for r in results if r[0] == seed]
        if not hit:
            raise SystemExit("seed %d not in the search results" % seed)
        _, score, info, p = hit[0]
        out.append({
            "id": "flow_search_%d" % seed,
            "name": "Flow-Lenia rule %d (found by search)" % seed,
            "status": "tuned",
            "source": "Found in this lab (prototype/6-lenia) by random search over the rule space of the published "
                      "Flow-Lenia code (FlowLenia.__init__ ranges; " + PLANTEC_REF + "): tools/lenia_lab/flowlenia_search.py, "
                      "numpy seed %d, C=1, %d kernels. Not published." % (seed, len(p["r"])),
            "notes": "Net drift in the search %.3f cells per time unit (96 x 96 torus, 600 steps): close to stationary. "
                     "Seeded with a uniform-noise square." % score,
            "model": "flow",
            "R": p["R"],
            "dt": 0.2, "dd": 5, "sigma": 0.65, "theta": 1.0,
            "channels": 1,
            "kernels": rings_kernels(p),
            "noisePatch": 20,
        })
    return out


def flow_species(seeds, search):
    out = []
    p = seeds["example_fl_seed1111"]
    out.append({
        "id": "flow_example_1111",
        "name": "Flow-Lenia example rule (seed 1111, 3 channels)",
        "status": "sampled",
        "source": PLANTEC_REF + "; rule drawn by the published examples/example_fl.py "
                  "(FlowLenia(cfg, key=jr.key(1111)), C=3, M=[[3,1,0],[0,3,1],[1,0,3]], X=Y=128), "
                  "draws reproduced with jax.random (threefry) by tools/lenia_lab/flowlenia_seeds.py.",
        "notes": "A random rule from the published example, not a curated creature. theta = C = 3 as in flowlenia.py; "
                 "dt 0.2, dd 5, sigma 0.65 (code defaults). Seeded as the example: a 40 x 40 uniform-noise square.",
        "model": "flow",
        "R": p["R"],
        "dt": 0.2, "dd": 5, "sigma": 0.65, "theta": 3.0,
        "channels": 3,
        "kernels": rings_kernels(p, p["c0"], p["c1"]),
        "noisePatch": 40,
    })
    for entry in (search or []):
        out.append(entry)
    return out


def worlds(embed_ids):
    rel = lambda f, t, m, s, h: {"from": f, "to": t, "shape": "shells", "b": "1", "kn": 1, "R": 13,
                                 "m": m, "s": s, "h": h, "gn": 2, "relative": True}
    return [
        {"id": "two_orbium_repel", "name": "Two Orbium species that repel (tuned)", "status": "tuned",
         "source": "Lab construction (prototype/6-lenia): two published Orbium unicaudatus in channels 0 and 1, "
                   "plus two relative cross kernels (lab extension). Cross-kernel values chosen in the lab, not published.",
         "notes": "Each sees the other through an Orbium-sized kernel (R 13); Gaussian growth with m 0, s 0.05, h 0.25, "
                  "relative: no effect when the other is absent, inhibition where it is present. Chosen by a GPU sweep "
                  "(h 0.15/0.25/0.5 x s 0.02/0.05 x 3 approach offsets): the only pair where both survived every offset; "
                  "they deflect. Stronger or narrower values kill both at some offsets.",
         "model": "lenia", "channels": ["orbium", "orbium"],
         "crossKernels": [rel(1, 0, 0.0, 0.05, 0.25), rel(0, 1, 0.0, 0.05, 0.25)]},
        {"id": "two_orbium_prey", "name": "Orbium prey and predator (tuned)", "status": "tuned",
         "source": "Lab construction (prototype/6-lenia): two published Orbium unicaudatus, relative cross kernels "
                   "(lab extension), values chosen in the lab, not published.",
         "notes": "Channel 0 (prey) is inhibited by channel 1 (m 0, s 0.02, h 1); channel 1 grows a little near channel 0 "
                  "(m 0.08, s 0.03, h 0.3). On contact the prey dissolves.",
         "model": "lenia", "channels": ["orbium", "orbium"],
         "crossKernels": [rel(1, 0, 0.0, 0.02, 1.0), rel(0, 1, 0.08, 0.03, 0.3)]},
    ] + ([{"id": "flow_embed", "name": "Two Flow-Lenia rules sharing matter (embedding, tuned)", "status": "tuned",
           "source": "Lab construction (prototype/6-lenia): parameter embedding as in Plantec et al. (2023) "
                     "(flowlenia_params.py: P travels with the matter, mixed at collisions), with two rules found by "
                     "the lab's search (not published).",
           "notes": "One matter channel; slot 0 and slot 1 carry the two rules. Where their matter meets, P mixes "
                    "(average or stochastic pick) and the local rule changes with it.",
           "model": "flow", "embed": True, "channels": list(embed_ids)}] if len(embed_ids) >= 2 else [])


def main():
    animals = json.load(open(sys.argv[1]))
    seeds = json.load(open(sys.argv[2]))
    search = []
    if len(sys.argv) > 4:
        chosen = [int(x) for x in sys.argv[4].split(",")]
        search = search_species(json.load(open(sys.argv[3])), chosen)
    lib = {
        "format": "Shifting Sands Lenia lab species v1",
        "about": "Presets for the Lenia lab (src/Games/LeniaLab.*, LeniaSpecies.*). status: published = copied from "
                 "the source named; sampled = drawn by published code from a seed, not a curated creature; tuned = "
                 "chosen in this lab. Written by tools/lenia_lab/make_species.py; edit that script, not this file.",
        "species": chan_species(animals) + flow_species(seeds, search),
        "worlds": worlds([e["id"] for e in search][:2]),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        json.dump(lib, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("wrote", os.path.relpath(OUT, REPO), len(lib["species"]), "species,", len(lib["worlds"]), "worlds")


if __name__ == "__main__":
    main()
