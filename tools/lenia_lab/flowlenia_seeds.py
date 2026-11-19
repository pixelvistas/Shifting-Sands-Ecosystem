#!/usr/bin/env python3
"""Reproduces the Flow-Lenia example rules for tools/lenia_lab/make_species.py
(branch prototype/6-lenia). Needs jax (pip install "jax[cpu]"). Does not
import or run the FlowLenia repository; the draws below copy what its
constructor does.

    python3 flowlenia_seeds.py   # writes flowlenia_seeds.json in the working directory
"""
# Reproduces the parameter draws of FlowLenia.__init__ (flowlenia/flowlenia.py,
# erwanplantec/FlowLenia main; read, not run):
#   kR,kr,km,ks,kh,ka,kb,kw = jr.split(key, 8); R ~ U(2,25); r ~ U(.2,1,(k,));
#   m ~ U(.05,.5); s ~ U(.001,.18); h ~ U(.01,1); a ~ U(0,1,(k,3));
#   b ~ U(.001,1,(k,3)); w ~ U(.01,.5,(k,3))
import json
import numpy as np
import jax
import jax.random as jr


def draws(seed, k):
    key = jr.key(seed)
    kR, kr, km, ks, kh, ka, kb, kw = jr.split(key, 8)
    p = dict(R=jr.uniform(kR, (), minval=2.0, maxval=25.0),
             r=jr.uniform(kr, (k,), minval=0.2, maxval=1.0),
             m=jr.uniform(km, (k,), minval=0.05, maxval=0.5),
             s=jr.uniform(ks, (k,), minval=0.001, maxval=0.18),
             h=jr.uniform(kh, (k,), minval=0.01, maxval=1.0),
             a=jr.uniform(ka, (k, 3), minval=0.0, maxval=1.0),
             b=jr.uniform(kb, (k, 3), minval=0.001, maxval=1.0),
             w=jr.uniform(kw, (k, 3), minval=0.01, maxval=0.5))
    return {n: np.asarray(v, dtype=np.float64).tolist() for n, v in p.items()}


def conn_from_matrix(M):
    C = len(M)
    c0 = []
    c1 = [[] for _ in range(C)]
    i = 0
    for s in range(C):
        for t in range(C):
            n = M[s][t]
            if n:
                c0 += [s] * n
                c1[t] += list(range(i, i + n))
            i += n
    return c0, c1


out = {}
for name, seed, M in [("example_fl_seed1111", 1111, [[3, 1, 0], [0, 3, 1], [1, 0, 3]]),
                      ("flowlenia_main_seed101", 101, [[2, 1, 0], [0, 2, 1], [1, 0, 2]])]:
    c0, c1 = conn_from_matrix(M)
    out[name] = dict(draws(seed, sum(map(sum, M))), c0=c0, c1=c1, M=M)
json.dump(out, open("flowlenia_seeds.json", "w"), indent=1)
print("jax", jax.__version__, jax.config.jax_default_prng_impl)
print(json.dumps(out["example_fl_seed1111"])[:700])
