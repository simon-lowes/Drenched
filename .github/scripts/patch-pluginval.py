"""Patches pluginval v1.0.4's copy of JUCE before it is built.

1. Backports JUCE 9's fix for a use-after-free in the Linux VST3 host run loop:
   an fd callback could run after the run loop was destroyed in the same event
   loop iteration, so pluginval crashed (about one run in fifteen) while
   unloading any plug-in, after every test had passed.
2. Includes <chrono> literals, which v1.0.4 uses without including and current
   MSVC rejects.

Usage: python3 patch-pluginval.py <pluginval source dir>
"""

import pathlib
import sys

root = pathlib.Path(sys.argv[1])

vst3 = root / "modules/juce/modules/juce_audio_processors/format_types/juce_VST3PluginFormat.cpp"
text = vst3.read_bytes().decode("utf-8")
crlf = "\r\n" in text
text = text.replace("\r\n", "\n")

replacements = [
    ("""            for (const auto& h : eventHandlerMap)
                LinuxEventLoop::unregisterFdCallback (h.first);""",
     """            for (const auto& h : *eventHandlerMap)
                LinuxEventLoop::unregisterFdCallback (h.first);"""),
    ("""            auto& handlers = eventHandlerMap[fd];

            if (handlers.empty())
            {
                LinuxEventLoop::registerFdCallback (fd, [this] (int descriptor)
                {
                    for (auto* h : eventHandlerMap[descriptor])
                        h->onFDIsSet (descriptor);

                    return true;
                });""",
     """            auto& handlers = (*eventHandlerMap)[fd];

            if (handlers.empty())
            {
                LinuxEventLoop::registerFdCallback (fd, [weak = std::weak_ptr { eventHandlerMap }] (int descriptor)
                {
                    if (const auto strong = weak.lock())
                    {
                        const auto iter = strong->find (descriptor);

                        if (iter == strong->end())
                            return true;

                        for (auto* h : iter->second)
                            h->onFDIsSet (descriptor);
                    }

                    return true;
                });"""),
    ("for (auto iter = eventHandlerMap.begin(), end = eventHandlerMap.end(); iter != end;)",
     "for (auto iter = eventHandlerMap->begin(), end = eventHandlerMap->end(); iter != end;)"),
    ("iter = eventHandlerMap.erase (iter);", "iter = eventHandlerMap->erase (iter);"),
    ("std::unordered_map<Linux::FileDescriptor, std::vector<Linux::IEventHandler*>> eventHandlerMap;",
     "std::shared_ptr<std::unordered_map<Linux::FileDescriptor, std::vector<Linux::IEventHandler*>>> eventHandlerMap\n"
     "            = std::make_shared<std::unordered_map<Linux::FileDescriptor, std::vector<Linux::IEventHandler*>>>();"),
]

for old, new in replacements:
    if text.count(old) != 1:
        sys.exit(f"patch-pluginval: expected one match for: {old.splitlines()[0].strip()}")
    text = text.replace(old, new)

vst3.write_bytes((text.replace("\n", "\r\n") if crlf else text).encode("utf-8"))

for source in ["Source/tests/BasicTests.cpp", "Source/Validator.cpp"]:
    path = root / source
    code = path.read_bytes().decode("utf-8")
    if "std::chrono_literals" not in code:
        path.write_bytes(("#include <chrono>\nusing namespace std::chrono_literals;\n" + code).encode("utf-8"))

print("patch-pluginval: patched", root)
