"""Writes the small SOFA files in this folder, which tests/SofaTests.cpp reads.

Needs h5py and NumPy (both BSD-3-Clause), only to write these files: nothing here is
built into Drenched or its tests, and the tests only read the files. Run it from anywhere:

    python3 tests/data/make_sofa_files.py

libmysofa reads only the HDF5 that netCDF-4 writes (the SOFA tools for MATLAB and Python
use netCDF), so the files are laid out as netCDF lays them out: the HDF5 1.8 file format
with link and attribute creation order tracked, the dimensions I, C, R, E, N and M as
netCDF dimension scales, 64-bit samples, string attributes scalar, of fixed length and
null-terminated, and compressed data shuffled before deflating (libmysofa always
unshuffles). Every response is quantised to 16 bits, so its silent end compresses away.
"""

import math
import os

import h5py
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 48000.0


def set_text(target, name, value):
    """A string attribute as netCDF writes one: scalar, fixed length, null-terminated ASCII."""
    data = value.encode("ascii")
    string = h5py.h5t.C_S1.copy()
    string.set_size(max(1, len(data)))
    string.set_strpad(h5py.h5t.STR_NULLTERM)
    attribute = h5py.h5a.create(target.id, name.encode("ascii"), string, h5py.h5s.create(h5py.h5s.SCALAR))
    attribute.write(np.array(data, dtype="S%d" % max(1, len(data))), mtype=string)


def dimension(f, name, size):
    d = f.create_dataset(name, data=np.zeros(size, dtype=">f4"))
    set_text(d, "CLASS", "DIMENSION_SCALE")
    set_text(d, "NAME", "This is a netCDF dimension but not a netCDF variable. %9d" % size)


def variable(f, name, values, **attributes):
    values = np.asarray(values, dtype="<f8")
    compress = values.size > 64
    d = f.create_dataset(name, data=values, compression="gzip" if compress else None, shuffle=compress,
                         chunks=values.shape if compress else None)

    for key, value in attributes.items():
        set_text(d, key, value)


def decaying(seed, length, rt60):
    """A room's response: noise falling 60 dB in rt60 seconds, quantised to 16 bits."""
    rng = np.random.default_rng(seed)
    n = np.arange(length)
    x = rng.uniform(-0.5, 0.5, length) * 10.0 ** (-3.0 * n / (rt60 * RATE))
    return np.round(x * 32767.0) / 32767.0


def write(name, conventions, ir, sources, receivers, emitters=None, ir_attributes=None, extra=None,
          source_type="spherical", receiver_type="cartesian", emitter_type="cartesian"):
    """ir is [M][R][N], or [M][R][E][N] with emitters; positions are rows of three."""
    ir = np.asarray(ir)
    M, R = ir.shape[0], ir.shape[1]
    E = ir.shape[2] if ir.ndim == 4 else 1
    N = ir.shape[-1]

    with h5py.File(os.path.join(HERE, name), "w", libver=("v108", "v108"), track_order=True) as f:
        attributes = {
            "Conventions": "SOFA",
            "Version": "2.1",
            "SOFAConventions": conventions,
            "SOFAConventionsVersion": "1.0",
            "APIName": "tests/data/make_sofa_files.py",
            "APIVersion": "1.0",
            "AuthorContact": "Drenched tests",
            "Organization": "Drenched",
            "License": "Made for Drenched's tests; same licence as Drenched",
            "DataType": "FIR",
            "RoomType": "reverberant",
            "Title": name,
        }
        attributes.update(extra or {})

        for key, value in attributes.items():
            set_text(f, key, value)

        for dim, size in (("I", 1), ("C", 3), ("R", R), ("E", E), ("N", N), ("M", M)):
            dimension(f, dim, size)

        variable(f, "ListenerPosition", [[0.0, 0.0, 0.0]], Type="cartesian", Units="metre")
        variable(f, "ListenerView", [[1.0, 0.0, 0.0]], Type="cartesian", Units="metre")
        variable(f, "ListenerUp", [[0.0, 0.0, 1.0]])
        units = {"spherical": "degree, degree, metre", "cartesian": "metre"}
        variable(f, "SourcePosition", sources, Type=source_type, Units=units[source_type])
        variable(f, "ReceiverPosition", np.asarray(receivers, dtype=float).reshape(R, 3, 1),
                 Type=receiver_type, Units=units[receiver_type])
        variable(f, "EmitterPosition", np.asarray(emitters if emitters is not None else [[0.0, 0.0, 0.0]],
                                                  dtype=float).reshape(E, 3, 1),
                 Type=emitter_type, Units=units[emitter_type])
        variable(f, "Data.IR", ir, **(ir_attributes or {}))
        variable(f, "Data.SamplingRate", [RATE], Units="hertz")
        variable(f, "Data.Delay", np.zeros((1, R) if E == 1 else (1, R, E)))


def tagged(m, r, e=0, length=32):
    """A short response whose first sample says which it is: 100 m + 10 e + r + 1."""
    x = decaying(1000 + 100 * m + 10 * e + r, length, 0.0005)
    x[0] = 100 * m + 10 * e + r + 1
    return x


def main():
    # Receivers placed by azimuth, elevation and distance: the right ear (azimuth -90,
    # SOFA's azimuth runs anticlockwise) listed first. Sources ahead and 30 degrees to
    # either side.
    write("spherical-receivers.sofa", "SingleRoomMIMOSRIR",
          [[tagged(m, r) for r in range(2)] for m in range(3)],
          sources=[[0.0, 0.0, 2.0], [30.0, 0.0, 2.0], [-30.0, 0.0, 2.0]],
          receivers=[[-90.0, 0.0, 0.09], [90.0, 0.0, 0.09]], receiver_type="spherical")

    # One loudspeaker 2 m ahead with two drivers, placed by azimuth, elevation and
    # distance from it: one 1 m to its left and one 1 m to its right.
    write("spherical-emitters.sofa", "SingleRoomMIMOSRIR",
          [[[tagged(0, r, e) for e in range(2)] for r in range(2)]],
          sources=[[2.0, 0.0, 0.0]], source_type="cartesian",
          receivers=[[0.0, 0.09, 0.0], [0.0, -0.09, 0.0]],
          emitters=[[90.0, 0.0, 1.0], [-90.0, 0.0, 1.0]], emitter_type="spherical")

    # One first-order B-format room response written in three conventions. In AmbiX
    # (ACN order W Y Z X, SN3D) its channels are these; FuMa orders them W X Y Z with W
    # 3 dB down, and N3D scales Y, Z and X by the square root of 3.
    length = 1200
    w, y, z, x = (decaying(seed, length, 0.012) * gain for seed, gain in ((1, 1.0), (2, 0.5), (3, 0.25), (4, 0.7)))
    root3 = math.sqrt(3.0)

    for name, channels, ordering, normalisation in (
            ("ambisonic-ambix.sofa", [w, y, z, x], "acn", "sn3d"),
            ("ambisonic-fuma.sofa", [w / math.sqrt(2.0), x, y, z], "fuma", "fuma"),
            ("ambisonic-n3d.sofa", [w, root3 * y, root3 * z, root3 * x], "acn", "n3d")):
        write(name, "AmbisonicsDRIR", [channels], sources=[[0.0, 0.0, 2.0]],
              receivers=[[0.0, 0.0, 0.0]] * 4,
              ir_attributes={"ChannelOrdering": ordering, "Normalization": normalisation},
              extra={"AmbisonicsOrder": "1"})


if __name__ == "__main__":
    main()
