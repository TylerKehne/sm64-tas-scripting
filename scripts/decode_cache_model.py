"""What a per-thread savestate cache would save of scattershot's block decoding (ROADMAP 4.3).

A model of the block tree, not a measurement. Blocks form a tree: a novel block found at
script n of a pellet fired from base block b gets a segment (parent = b's tail segment,
nScripts = n + 1), and decoding a block replays every segment on its chain from the root
(ScattershotThread::DecodeBaseBlockDiffAndApply). A shot picks a uniformly random block as
its base, the root every --root-every shots (SelectBaseBlock). Each thread keeps a
least-recently-used cache of decoded segment states; a decode starts at the deepest cached
ancestor of the chain and caches every segment it passes through. A LibSm64 state cannot
leave the DLL copy it was taken from (docs/libsm64.md), so the caches are per thread.

The defaults are the dr-oscillations stage's first pass as profiled on 2026-09-14
(docs/performance-changelog.md): 30,000 shots, 16 threads, 200 pellets per shot, 462
scripts per shot (2.3 per pellet), 281,650 blocks (about 9.4 novel per shot late in the
run). The tilt-target Tier D run is --shots 600 --threads 8 --pellets 100
--scripts-per-pellet 8.7 --novel-per-shot 186. Output: per cache size, the scripts a decode
replays per shot with and without the cache, the share saved, and the saves the cache costs
per shot.

    python scripts\\decode_cache_model.py
    python scripts\\decode_cache_model.py --shots 200000
"""
import argparse
import random
from collections import OrderedDict


def simulate(shots, threads, pellets_per_shot, mean_scripts_per_pellet, novel_per_shot_late,
             cache_size, root_every=100, seed=1, cache_intermediate=True):
    rng = random.Random(seed)
    parents = [-1]   # segment id -> parent segment id (the root is 0)
    nscripts = [0]   # segment id -> scripts it replays
    blocks = [0]     # block index -> tail segment id
    caches = [OrderedDict() for _ in range(threads)]
    replayed = 0
    replayed_nocache = 0
    saves = 0
    windows = []
    win_replayed = win_nocache = win_saves = win_shots = 0
    for shot in range(shots):
        t = shot % threads
        base = 0 if shot % root_every == 0 else rng.randrange(len(blocks))
        tail = blocks[base]
        chain = []
        s = tail
        while s != 0:
            chain.append(s)
            s = parents[s]
        chain.reverse()  # root-most first
        cache = caches[t]
        start = 0
        for i in range(len(chain) - 1, -1, -1):
            if chain[i] in cache:
                start = i + 1
                cache.move_to_end(chain[i])
                break
        cost = sum(nscripts[s] for s in chain[start:])
        full = sum(nscripts[s] for s in chain)
        replayed += cost
        replayed_nocache += full
        win_replayed += cost
        win_nocache += full
        win_shots += 1
        if cache_size > 0:
            for s in (chain[start:] if cache_intermediate else chain[-1:]):
                if s not in cache:
                    cache[s] = True
                    saves += 1
                    win_saves += 1
                    if len(cache) > cache_size:
                        cache.popitem(last=False)
        # Pellets: the novelty rate starts near 1 and settles at the run's late rate.
        novel_rate = novel_per_shot_late / (pellets_per_shot * mean_scripts_per_pellet)
        p_novel = max(novel_rate, 1.0 / (1.0 + shot / 30.0))
        for _ in range(pellets_per_shot):
            length = min(20, max(1, int(rng.expovariate(1.0 / mean_scripts_per_pellet))))
            for n in range(length):
                if rng.random() < p_novel:
                    parents.append(tail)
                    nscripts.append(n + 1)
                    blocks.append(len(parents) - 1)
        if (shot + 1) % max(1, shots // 5) == 0:
            windows.append((shot + 1, len(blocks), win_replayed / win_shots, win_nocache / win_shots, win_saves / win_shots))
            win_replayed = win_nocache = win_saves = win_shots = 0
    return replayed, replayed_nocache, saves, len(blocks), windows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--shots", type=int, default=30000)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--pellets", type=int, default=200, help="pellets per shot")
    parser.add_argument("--scripts-per-pellet", type=float, default=2.3, help="mean scripts a pellet runs")
    parser.add_argument("--novel-per-shot", type=float, default=9.4, help="novel blocks per shot late in the run")
    parser.add_argument("--root-every", type=int, default=100, help="StartFromRootEveryNShots")
    parser.add_argument("--cache", type=int, nargs="+", default=[0, 16, 64, 256, 1024, 4096], help="cache sizes per thread")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    print(f"shots={args.shots} threads={args.threads} pellets/shot={args.pellets} "
          f"scripts/pellet={args.scripts_per_pellet} novel/shot(late)={args.novel_per_shot} root every {args.root_every}")
    print(f"{'cache/thread':>12} {'blocks':>8} {'replay scripts/shot':>20} {'no cache':>9} {'saved':>6} {'saves/shot':>10}")
    for c in args.cache:
        r, r0, saves, blocks, windows = simulate(args.shots, args.threads, args.pellets, args.scripts_per_pellet,
                                                 args.novel_per_shot, c, args.root_every, args.seed)
        n = args.shots
        print(f"{c:>12} {blocks:>8} {r / n:>20.1f} {r0 / n:>9.1f} {1 - r / r0:>6.0%} {saves / n:>10.2f}")
        for w in windows:
            print(f"    to shot {w[0]:>7}: blocks {w[1]:>8}, replay/shot {w[2]:>6.1f} (no cache {w[3]:>6.1f}), saves/shot {w[4]:.2f}")


if __name__ == "__main__":
    main()
