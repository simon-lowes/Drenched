# Scene model

`places365.drsn` is what Describe a room looks at a photograph with: the
Places365 scene recognition network (ResNet-18, 224 x 224 in, the Places
release's "wideresnet18"), with the SUN scene-attribute weights, converted
into a compact file the plugin reads itself (`source/engine/src/SceneNet.cpp`:
plain C++, no machine-learning runtime, nothing sent anywhere).

It is published by MIT CSAIL under CC BY and must stay credited in the plugin
(Recording credits) and in `NOTICE.md`:

> Bolei Zhou, Agata Lapedriza, Aditya Khosla, Aude Oliva and Antonio Torralba,
> "Places: A 10 million Image Database for Scene Recognition", IEEE TPAMI 2017.
> http://places2.csail.mit.edu

What the model says (365 kinds of place, 102 attributes such as carpet, brick,
glass, enclosed, natural light) becomes words for the room describer
(`sceneWords()` in `SceneNet.cpp`), and the describer works the room out from
those as it would from typed words.

## Rebuilding

Download from the Places project into one folder: `wideresnet18_places365.pth.tar`,
`categories_places365.txt`, `IO_places365.txt`, `labels_sunattribute.txt` and
`W_sceneattribute_wideresnet18.npy`. Then, with torch and numpy installed:

    python3 tools/scene/convert_places365.py <folder> library/scene/places365.drsn

The file is 11.6 MB: every convolution's weights as 8-bit integers with a
scale per output channel and its batch normalisation folded into a bias, the
classifier, the attribute weights and the labels. The conversion is
deterministic; `tests/SceneNetTests.cpp` checks what the plugin's reader makes
of the test photographs in `tests/data/scene/` against what the published
network says of them.
