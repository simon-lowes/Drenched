# Test data

Small files made for Drenched's own tests, read from here by `drenched_tests`
(`DRENCHED_TEST_DATA_DIR`, set in `tests/CMakeLists.txt`). They are not shipped.

| File | What it covers |
| --- | --- |
| `spherical-receivers.sofa` | Receivers placed by azimuth, elevation and distance (the right one first); three sources, ahead and 30 degrees either side |
| `spherical-emitters.sofa` | One loudspeaker with two drivers placed by azimuth, elevation and distance from it |
| `ambisonic-ambix.sofa`, `ambisonic-fuma.sofa`, `ambisonic-n3d.sofa` | One first-order B-format room response written as AmbiX (ACN, SN3D), FuMa and N3D, said by the `ChannelOrdering` and `Normalization` attributes of `Data.IR` |

Every response's first sample tags which measurement, emitter and receiver it is (see
`tests/SofaTests.cpp`). The SOFA files are written by `make_sofa_files.py` in this
folder, which needs h5py and NumPy; run it again after changing it and commit the files
with it. Its comments say why the files are laid out as they are: libmysofa reads only
HDF5 laid out the way netCDF-4 writes it.
