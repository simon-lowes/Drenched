#include "RecordingCache.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <sys/stat.h>
#endif

namespace drenched
{

juce::String RecordingCache::changeStampOf (const juce::File& file)
{
    const auto number = [] (auto n) { return juce::String (static_cast<juce::int64> (n)); };

   #if JUCE_WINDOWS
    auto* handle = CreateFileW (file.getFullPathName().toWideCharPointer(), FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (handle == INVALID_HANDLE_VALUE)
        return "missing";

    BY_HANDLE_FILE_INFORMATION info {};
    FILE_BASIC_INFO times {};
    const auto read = GetFileInformationByHandle (handle, &info) != 0
                   && GetFileInformationByHandleEx (handle, FileBasicInfo, &times, sizeof (times)) != 0;
    CloseHandle (handle);

    if (! read)
        return juce::String (file.getLastModificationTime().toMilliseconds());

    const auto index = (static_cast<juce::uint64> (info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    return number (info.dwVolumeSerialNumber) + ":" + juce::String (static_cast<juce::int64> (index))
         + "|" + number (times.LastWriteTime.QuadPart) + "|" + number (times.ChangeTime.QuadPart);
   #else
    struct stat info {};

    if (stat (file.getFullPathName().toRawUTF8(), &info) != 0)
        return "missing";

   #if JUCE_MAC || JUCE_IOS
    const auto& modified = info.st_mtimespec;
    const auto& changed = info.st_ctimespec;
   #else
    const auto& modified = info.st_mtim;
    const auto& changed = info.st_ctim;
   #endif

    const auto stamp = [&number] (const timespec& t) { return number (t.tv_sec) + "." + number (t.tv_nsec); };

    return number (info.st_dev) + ":" + number (info.st_ino) + "|" + stamp (modified) + "|" + stamp (changed);
   #endif
}

} // namespace drenched
