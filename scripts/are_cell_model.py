"""Where Mario has to stand for the pyramid to hold a wanted adjusted remainder error (ROADMAP 4.8).

A model in float32 arithmetic, not a measurement. With Mario at rest on the pyramid its
normal converges to the goal his position defines (bhv_tilting_inverted_pyramid_loop,
tasfw-core/src/decomp/Pyramid.cpp: dx = mx - px, dz = mz - pz, d = sqrtf(dx*dx + 500*500 +
dz*dz), d = (float)(1.0 / d), nx = dx * d, nz = dz * d, and approach_by_increment snaps to
the goal once within 0.01 of it), so the resting normal is a pure function of the rest
position. The adjusted remainder error (`CalculateARE` in TiltTargetShot.hpp) steps that
normal by 0.01f toward the target until within 0.005 of it and measures the miss in ULPs of
the target, so it is a pure function of the rest position too. This script sweeps the rest
position along one axis, one float32 value at a time, and prints how the error moves per
float step, how wide the bands of positions inside a tolerance are, how far apart they sit,
and how much the other axis's error drifts along the sweep (the coupling through d, which
is what fills the lattice in between).

    python scripts\\are_cell_model.py
    python scripts\\are_cell_model.py --dx -80 --dz -25 --units 6 --tolerance 100
"""
import argparse
import math
import struct


def f32(x):
    """Round a Python float to the nearest float32 (as a Python float)."""
    return struct.unpack('f', struct.pack('f', x))[0]


def next32(x):
    """The float32 after x."""
    bits = struct.unpack('I', struct.pack('f', x))[0]
    bits = bits + 1 if x >= 0 else bits - 1
    return struct.unpack('f', struct.pack('I', bits))[0]


def ulp32(x):
    _, e = math.frexp(x)
    return math.ldexp(1.0, e - 24)


STEP = f32(0.01)
HALF_STEP = f32(0.005)


def resting_normal(mx, mz, px, pz):
    dx = f32(mx - px)
    dz = f32(mz - pz)
    d = f32(math.sqrt(f32(f32(f32(dx * dx) + f32(500.0 * 500.0)) + f32(dz * dz))))
    d = f32(1.0 / d)
    return f32(dx * d), f32(dz * d)


def are(v, target):
    """CalculateARE for one axis: (error in ULPs of the target, steps of 0.01 taken)."""
    error = f32(target - v)
    step = STEP if error > 0 else -STEP
    for i in range(200):
        if abs(f32(target - v)) <= HALF_STEP:
            return f32(f32(target - v) / ulp32(target)), i
        v = f32(v + step)
    return math.inf, 200


def bands(hits):
    """[(first index, width)] of the runs of True in a list."""
    out = []
    start = None
    for i, hit in enumerate(hits + [False]):
        if hit and start is None:
            start = i
        elif not hit and start is not None:
            out.append((start, i - start))
            start = None
    return out


def sweep(axis, args):
    px, pz = args.px, args.pz
    tx, tz = f32(args.target_nx), f32(args.target_nz)
    x0, z0 = f32(px + args.dx), f32(pz + args.dz)
    pos = x0 if axis == 'x' else z0
    step = ulp32(pos)
    count = int(args.units / step)
    errors, others, steps = [], [], []
    p = pos
    for _ in range(count):
        mx, mz = (p, z0) if axis == 'x' else (x0, p)
        nx, nz = resting_normal(mx, mz, px, pz)
        ax, kx = are(nx, tx)
        az, kz = are(nz, tz)
        errors.append(ax if axis == 'x' else az)
        others.append(az if axis == 'x' else ax)
        steps.append(kx if axis == 'x' else kz)
        p = next32(p)
    diffs = sorted(abs(b - a) for a, b in zip(errors, errors[1:]))
    slope = diffs[len(diffs) // 2]  # the median: the wrap at a band's edge is a huge jump
    other_diffs = sorted(abs(b - a) for a, b in zip(others, others[1:]))
    print(f"\n{axis} sweep: {count} float32 positions over {args.units} units from {pos!r} "
          f"(one float step is {step:.3g} units); {min(steps)}..{max(steps)} steps of 0.01 to the target")
    print(f"  ARE_{axis} moves {slope:.0f} ULPs per float step of {axis}; the other axis's ARE drifts "
          f"{(max(others) - min(others)) / max(count - 1, 1):.3f} ULPs per step, "
          f"{max(other_diffs):.0f} at most in one step")
    for tolerance in (args.tolerance, 10, 1, 0):
        runs = bands([abs(e) <= tolerance for e in errors])
        if not runs:
            print(f"  |ARE_{axis}| <= {tolerance:3d}: no band in the sweep")
            continue
        width = sum(w for _, w in runs) / len(runs)
        spacing = ((runs[-1][0] - runs[0][0]) / (len(runs) - 1) * step) if len(runs) > 1 else math.nan
        print(f"  |ARE_{axis}| <= {tolerance:3d}: {len(runs)} band(s), {width:.1f} positions "
              f"({width * step:.2e} units) wide, {spacing:.2f} units apart")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--px', type=float, default=-1945.0, help='pyramid home x (BitFsObjects.hpp)')
    parser.add_argument('--pz', type=float, default=-715.0, help='pyramid home z')
    parser.add_argument('--target-nx', type=float, default=-0.17944, help='target normal x (config.json)')
    parser.add_argument('--target-nz', type=float, default=0.3936, help='target normal z')
    parser.add_argument('--dx', type=float, default=-80.0, help='rest position x, relative to the home')
    parser.add_argument('--dz', type=float, default=-25.0, help='rest position z, relative to the home')
    parser.add_argument('--units', type=float, default=6.0, help='length of each sweep in game units')
    parser.add_argument('--tolerance', type=int, default=100, help='the ARE neighborhood, in ULPs')
    args = parser.parse_args()
    nx, nz = resting_normal(f32(args.px + args.dx), f32(args.pz + args.dz), args.px, args.pz)
    print(f"rest position ({args.px + args.dx}, {args.pz + args.dz}): resting normal ({nx:.6f}, {nz:.6f}), "
          f"target ({args.target_nx}, {args.target_nz}), ULP of the target {ulp32(args.target_nx):.3g} in x, "
          f"{ulp32(args.target_nz):.3g} in z")
    sweep('x', args)
    sweep('z', args)


if __name__ == '__main__':
    main()
