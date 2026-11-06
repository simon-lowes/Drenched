#!/usr/bin/env python3
"""Turns the Places365 scene model (ResNet-18, CC BY, MIT CSAIL) into the compact
file the plugin reads: library/scene/places365.drsn.

The plugin runs the network itself (source/engine/src/SceneNet.cpp), so the file holds
only what that needs: every convolution's weights as 8-bit integers with a scale per
output channel and its batch normalisation folded into a bias, the classifier, the
scene-attribute weights, the labels, and which categories are indoors.

Needs torch and numpy (CPU is fine) and the published files, downloaded from the
Places project (http://places2.csail.mit.edu) into one folder:

    wideresnet18_places365.pth.tar     the trained network
    categories_places365.txt           365 category names
    IO_places365.txt                   which categories are indoors (1) or outdoors (2)
    labels_sunattribute.txt            102 scene attributes
    W_sceneattribute_wideresnet18.npy  the attributes' weights on the pooled features

    python3 tools/scene/convert_places365.py <folder> library/scene/places365.drsn

The network is the standard ResNet-18 without its max pool (the Places release calls
it wideresnet18), 224 x 224 in, so the last feature maps are 14 x 14.
"""

import os
import struct
import sys

import numpy as np
import torch

MAGIC = b"DRSN"
VERSION = 1


def fold(conv, bn):
    """Folds a batch norm into the convolution before it: y = g (x - m) / sqrt (v + e) + b."""
    w = conv.float().numpy()
    gamma, beta, mean, var = (bn[k].float().numpy() for k in ("weight", "bias", "running_mean", "running_var"))
    scale = gamma / np.sqrt(var + 1e-5)
    return w * scale[:, None, None, None], beta - mean * scale


def quantise(w):
    """8-bit weights with a scale per output channel (symmetric)."""
    flat = w.reshape(w.shape[0], -1)
    scale = np.maximum(np.abs(flat).max(axis=1), 1e-12) / 127.0
    q = np.clip(np.round(flat / scale[:, None]), -127, 127).astype(np.int8)
    return q, scale.astype(np.float32)


def main(folder, out_path):
    ck = torch.load(os.path.join(folder, "wideresnet18_places365.pth.tar"), map_location="cpu", weights_only=True)
    sd = {k.replace("module.", ""): v for k, v in ck["state_dict"].items()}

    classes = [line.split(" ")[0][3:] for line in open(os.path.join(folder, "categories_places365.txt"))]
    indoors = [int(line.split()[-1]) == 1 for line in open(os.path.join(folder, "IO_places365.txt"))]
    attributes = [line.rstrip("\n") for line in open(os.path.join(folder, "labels_sunattribute.txt"))]
    w_attr = np.load(os.path.join(folder, "W_sceneattribute_wideresnet18.npy")).astype(np.float32)
    assert len(classes) == 365 and len(indoors) == 365 and len(attributes) == 102 and w_attr.shape == (102, 512)

    def bn(prefix):
        return {k: sd[prefix + "." + k] for k in ("weight", "bias", "running_mean", "running_var")}

    # The convolutions in the order the network runs them.
    layers = [("conv1", sd["conv1.weight"], bn("bn1"), 2, 3)]

    for stage in range(1, 5):
        for block in range(2):
            p = "layer%d.%d" % (stage, block)
            stride = 2 if stage > 1 and block == 0 else 1
            layers.append((p + ".conv1", sd[p + ".conv1.weight"], bn(p + ".bn1"), stride, 1))
            layers.append((p + ".conv2", sd[p + ".conv2.weight"], bn(p + ".bn2"), 1, 1))

            if stride == 2:
                layers.append((p + ".downsample", sd[p + ".downsample.0.weight"], bn(p + ".downsample.1"), 2, 0))

    with open(out_path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<IIII", VERSION, len(layers), len(classes), len(attributes)))

        for names in (classes, attributes):
            blob = "\n".join(names).encode("ascii")
            f.write(struct.pack("<I", len(blob)))
            f.write(blob)

        f.write(bytes(1 if x else 0 for x in indoors))

        for name, conv, norm, stride, pad in layers:
            w, bias = fold(conv, norm)
            q, scale = quantise(w)
            out_c, in_c, k, _ = w.shape
            name_bytes = name.encode("ascii")
            f.write(struct.pack("<I", len(name_bytes)))
            f.write(name_bytes)
            f.write(struct.pack("<IIIII", out_c, in_c, k, stride, pad))
            f.write(q.tobytes())
            f.write(scale.tobytes())
            f.write(bias.astype(np.float32).tobytes())

        fc_q, fc_scale = quantise(sd["fc.weight"].float().numpy())
        f.write(struct.pack("<II", fc_q.shape[0], fc_q.shape[1]))
        f.write(fc_q.tobytes())
        f.write(fc_scale.tobytes())
        f.write(sd["fc.bias"].float().numpy().astype(np.float32).tobytes())
        f.write(w_attr.tobytes())

    print("wrote", out_path, os.path.getsize(out_path), "bytes,", len(layers), "convolutions")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    main(sys.argv[1], sys.argv[2])
