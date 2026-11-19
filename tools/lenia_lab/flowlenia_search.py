#!/usr/bin/env python3
"""Random search for moving, persistent one-channel Flow-Lenia patterns over the
rule space of the published code (FlowLenia.__init__ ranges); branch
prototype/6-lenia. Uses reference.py (NumPy). Prints one line per rule and
writes the 15 best to flsearch_<start>.json.

    python3 flowlenia_search.py <first seed> <count>
"""
# Random search for moving, persistent single-channel Flow-Lenia patterns, sampling the
# rule space of the published code (FlowLenia.__init__ ranges), C=1, k kernels.
import json, sys, numpy as np
from multiprocessing import Pool
sys.path.insert(0, '.')
from reference import FL
from reference import com, unwrap

N = 96

def components(mask):
    from scipy.ndimage import label
    lab, n = label(mask, structure=np.ones((3, 3)))
    return lab, n

def trial(seed):
    rng = np.random.default_rng(seed)
    k = 6
    p = dict(R=float(rng.uniform(2, 25)), r=rng.uniform(.2, 1, k).tolist(), m=rng.uniform(.05, .5, k).tolist(),
             s=rng.uniform(.001, .18, k).tolist(), h=rng.uniform(.01, 1, k).tolist(),
             a=rng.uniform(0, 1, (k, 3)).tolist(), b=rng.uniform(.001, 1, (k, 3)).tolist(), w=rng.uniform(.01, .5, (k, 3)).tolist(),
             c0=[0] * k, c1=[list(range(k))], M=[[k]])
    fl = FL(p, N, 1, border='torus')
    A = np.zeros((N, N, 1)); A[38:58, 38:58, 0] = rng.uniform(size=(20, 20))
    pos = []
    try:
        for i in range(600):
            A = fl.step(A)
            if i >= 300 and i % 20 == 0:
                pos.append(com(A[..., 0]))
    except Exception as e:
        return seed, -1, str(e), p
    mask = A[..., 0] > 0.1
    lab, n = components(mask)
    occ = mask.sum() / mask.size
    d = np.array([unwrap(pos[i], pos[i + 1], N) for i in range(len(pos) - 1)])
    net = np.hypot(*d.sum(0)) / (len(d) * 20 * 0.2)
    score = net if (1 <= n <= 3 and occ < 0.12) else 0.0
    return seed, float(score), dict(n=int(n), occ=float(occ), net=float(net)), p

if __name__ == '__main__':
    start, count = int(sys.argv[1]), int(sys.argv[2])
    with Pool(3) as pool:
        res = []
        for r in pool.imap_unordered(trial, range(start, start + count)):
            res.append(r)
            print(r[0], round(r[1], 4), r[2], flush=True)
    res.sort(key=lambda r: -r[1])
    json.dump(res[:15], open(f'flsearch_{start}.json', 'w'))
