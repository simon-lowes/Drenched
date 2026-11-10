#include "CrashReports.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator>
#include <mutex>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <dlfcn.h>
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace drenched::crashReports
{

namespace
{
    //==========================================================================
    // What the handler reads, all prepared before any crash.

    constexpr int stepCount = 16;
    constexpr size_t stepLength = 160;

    struct Step
    {
        std::atomic<std::int64_t> when { 0 }; // ms since 1970, 0: empty
        std::array<char, stepLength> text {};
    };

    std::array<Step, stepCount> steps;
    std::atomic<std::uint32_t> nextStep { 0 };
    std::mutex stepsLock; // between step() and recentSteps(); the crash handler reads without it

    constexpr size_t pathLength = 2048;
    constexpr size_t aboutLength = 2048;

    std::array<char, pathLength> folderPath {};      // ends with a separator
    std::array<char, aboutLength> aboutText {};
    std::atomic<bool> installed { false };
    std::atomic<bool> writing { false };             // one report per process

   #if JUCE_WINDOWS
    std::uintptr_t moduleStart = 0, moduleEnd = 0;   // this module's image, for "is it ours?"

    bool isOurs (std::uintptr_t address) noexcept
    {
        return address >= moduleStart && address < moduleEnd;
    }
   #else
    const void* moduleBase = nullptr;                // this module, as dladdr names it

    bool isOurs (std::uintptr_t address) noexcept
    {
        Dl_info info {};
        return moduleBase != nullptr && dladdr (reinterpret_cast<void*> (address), &info) != 0 && info.dli_fbase == moduleBase;
    }
   #endif

    void copyText (char* destination, size_t size, const char* source) noexcept
    {
        size_t i = 0;

        for (; i + 1 < size && source[i] != 0; ++i)
            destination[i] = source[i];

        destination[i] = 0;
    }

    //==========================================================================
    // Writing without allocating: a small buffer that's flushed to the file.

    struct Writer
    {
       #if JUCE_WINDOWS
        HANDLE file = INVALID_HANDLE_VALUE;
       #else
        int file = -1;
       #endif
        char buffer[1024];
        size_t used = 0;

        void flush() noexcept
        {
           #if JUCE_WINDOWS
            DWORD written = 0;
            if (used > 0)
                WriteFile (file, buffer, static_cast<DWORD> (used), &written, nullptr);
           #else
            size_t done = 0;
            while (done < used)
            {
                const auto n = ::write (file, buffer + done, used - done);
                if (n <= 0)
                    break;
                done += static_cast<size_t> (n);
            }
           #endif
            used = 0;
        }

        Writer& operator<< (const char* text) noexcept
        {
            for (; text != nullptr && *text != 0; ++text)
            {
                if (used == sizeof (buffer))
                    flush();
                buffer[used++] = *text;
            }
            return *this;
        }

        Writer& number (std::uint64_t value, unsigned base = 10) noexcept
        {
            char digits[24];
            int n = 0;
            do
            {
                const auto d = static_cast<unsigned> (value % base);
                digits[n++] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
                value /= base;
            } while (value != 0 && n < 24);

            char text[27];
            int i = 0;
            if (base == 16)
            {
                text[i++] = '0';
                text[i++] = 'x';
            }
            while (n > 0)
                text[i++] = digits[--n];
            text[i] = 0;
            return *this << text;
        }

        Writer& hex (std::uintptr_t value) noexcept { return number (value, 16); }
    };

    const char* baseName (const char* path) noexcept
    {
        const char* name = path;
        for (const char* p = path; p != nullptr && *p != 0; ++p)
            if (*p == '/' || *p == '\\')
                name = p + 1;
        return name;
    }

    void writeSteps (Writer& out) noexcept
    {
        out << "\nWhat Drenched last did (oldest first, ms since 1970):\n";
        const auto end = nextStep.load();

        for (std::uint32_t i = end > stepCount ? end - stepCount : 0; i < end; ++i)
        {
            const auto& s = steps[i % stepCount];
            const auto when = s.when.load();
            if (when == 0)
                continue;
            char text[stepLength];
            std::memcpy (text, s.text.data(), stepLength);
            text[stepLength - 1] = 0;
            out.number (static_cast<std::uint64_t> (when)) << " " << text << "\n";
        }
    }

    // The process's id, read when the handlers go in.
    std::uint64_t processId = 0;

    void appendNumber (char* path, size_t& length, size_t size, std::uint64_t t) noexcept
    {
        char digits[24];
        int n = 0;
        do { digits[n++] = static_cast<char> ('0' + t % 10); t /= 10; } while (t != 0 && n < 24);
        while (n > 0 && length + 1 < size)
            path[length++] = digits[--n];
    }

    // crash-<seconds since 1970>-<process id>.txt in the folder: a host and its plugin
    // processes share the folder, and a shared cause can crash two of them in one second.
    void reportPath (char* path, size_t size) noexcept
    {
        copyText (path, size, folderPath.data());
        auto length = std::strlen (path);
        const char* prefix = "crash-";
        for (; *prefix != 0 && length + 1 < size; ++prefix)
            path[length++] = *prefix;

        appendNumber (path, length, size, static_cast<std::uint64_t> (std::time (nullptr)));
        if (length + 1 < size)
            path[length++] = '-';
        appendNumber (path, length, size, processId);

        const char* suffix = ".txt";
        for (; *suffix != 0 && length + 1 < size; ++suffix)
            path[length++] = *suffix;
        path[length] = 0;
    }

    //==========================================================================
   #if JUCE_WINDOWS

    PVOID vectoredHandler = nullptr;

    // The report's writer, here rather than on the stack (see writeReport) and made when the
    // module loads, so the handler never runs a first-use initialiser.
    Writer reportWriter;
    void (__cdecl* previousAbort) (int) = nullptr;
    bool abortInstalled = false;

    const char* exceptionName (DWORD code) noexcept
    {
        switch (code)
        {
            case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
            case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
            case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
            case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
            case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned data";
            default:                              return nullptr;
        }
    }

    void writeModuleAndOffset (Writer& out, std::uintptr_t address) noexcept
    {
        HMODULE module = nullptr;

        if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR> (address), &module) && module != nullptr)
        {
            static char name[MAX_PATH];
            name[0] = 0;
            GetModuleFileNameA (module, name, MAX_PATH);
            out << (isOurs (address) ? "Drenched" : baseName (name)) << "+";
            out.hex (address - reinterpret_cast<std::uintptr_t> (module));
        }
        else
        {
            out.hex (address);
        }
    }

    void writeReport (const char* what, std::uintptr_t faultAddress, std::uintptr_t accessAddress) noexcept
    {
        if (writing.exchange (true))
            return;

        // Static, not on the stack: a stack overflow is handled on the thread whose stack
        // has just run out (writing lets only one thread in here).
        static char path[pathLength + 40];
        reportPath (path, sizeof (path));

        static wchar_t widePath[pathLength + 40];
        if (MultiByteToWideChar (CP_UTF8, 0, path, -1, widePath, static_cast<int> (std::size (widePath))) == 0)
            return;

        auto& out = reportWriter;
        out.used = 0;
        out.file = CreateFileW (widePath, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out.file == INVALID_HANDLE_VALUE)
            return;

        out << "Drenched crashed: " << what;
        if (faultAddress != 0)
        {
            out << " at ";
            writeModuleAndOffset (out, faultAddress);
        }
        if (accessAddress != 0)
        {
            out << ", address ";
            out.hex (accessAddress);
        }
        out << "\n" << aboutText.data() << "\n\nStack (module+offset):\n";

        static void* frames[48];
        const auto count = RtlCaptureStackBackTrace (0, 48, frames, nullptr);
        for (USHORT i = 0; i < count; ++i)
        {
            out << " ";
            out.number (i) << " ";
            writeModuleAndOffset (out, reinterpret_cast<std::uintptr_t> (frames[i]));
            out << "\n";
        }

        writeSteps (out);
        out.flush();
        CloseHandle (out.file);
    }

    LONG CALLBACK onException (EXCEPTION_POINTERS* info)
    {
        const auto* record = info != nullptr ? info->ExceptionRecord : nullptr;
        if (record == nullptr)
            return EXCEPTION_CONTINUE_SEARCH;

        const auto* name = exceptionName (record->ExceptionCode);
        const auto address = reinterpret_cast<std::uintptr_t> (record->ExceptionAddress);

        // Only fatal exceptions in this module's own code. Others (a C++ throw, a host's
        // own fault, a probe the host catches on purpose) are none of ours.
        if (name != nullptr && isOurs (address))
        {
            const auto access = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2
                                    ? static_cast<std::uintptr_t> (record->ExceptionInformation[1]) : 0;
            writeReport (name, address, access);
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void __cdecl onAbort (int)
    {
        writeReport ("abort()", 0, 0);
        std::signal (SIGABRT, previousAbort != nullptr ? previousAbort : SIG_DFL);
        std::raise (SIGABRT);
    }

    void installHandlers() noexcept
    {
        HMODULE module = nullptr;
        if (! GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCSTR> (&installHandlers), &module) || module == nullptr)
            return;

        const auto base = reinterpret_cast<std::uintptr_t> (module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*> (base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*> (base + static_cast<std::uintptr_t> (dos->e_lfanew));
        moduleStart = base;
        moduleEnd = base + nt->OptionalHeader.SizeOfImage;
        processId = GetCurrentProcessId();

        vectoredHandler = AddVectoredExceptionHandler (0, onException);
        previousAbort = std::signal (SIGABRT, onAbort);
        abortInstalled = previousAbort != SIG_ERR;
    }

    void removeHandlers() noexcept
    {
        if (vectoredHandler != nullptr)
            RemoveVectoredExceptionHandler (vectoredHandler);
        vectoredHandler = nullptr;

        if (abortInstalled)
        {
            // Only if it is still ours: something later may have put its own in.
            const auto current = std::signal (SIGABRT, previousAbort);
            if (current != onAbort && current != SIG_ERR)
                std::signal (SIGABRT, current);
        }
        abortInstalled = false;
    }

    //==========================================================================
   #else

    constexpr std::array<int, 5> fatalSignals { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    std::array<struct sigaction, fatalSignals.size()> previous {};
    std::array<bool, fatalSignals.size()> replaced {};

    const char* signalName (int sig) noexcept
    {
        switch (sig)
        {
            case SIGSEGV: return "signal 11 (SIGSEGV, bad memory access)";
            case SIGBUS:  return "SIGBUS (bad memory access)";
            case SIGILL:  return "SIGILL (illegal instruction)";
            case SIGFPE:  return "SIGFPE (arithmetic error)";
            case SIGABRT: return "SIGABRT (abort)";
            default:      return "a fatal signal";
        }
    }

    void writeFrame (Writer& out, void* frame) noexcept
    {
        const auto address = reinterpret_cast<std::uintptr_t> (frame);
        Dl_info info {};

        if (dladdr (frame, &info) != 0 && info.dli_fbase != nullptr)
        {
            out << (info.dli_fbase == moduleBase ? "Drenched" : baseName (info.dli_fname)) << "+";
            out.hex (address - reinterpret_cast<std::uintptr_t> (info.dli_fbase));
            if (info.dli_sname != nullptr)
                out << " " << info.dli_sname;
        }
        else
        {
            out.hex (address);
        }
    }

    /** Puts back what handled sig before; true if that is the default action, which
        ends the process. */
    bool putBack (int sig) noexcept
    {
        for (size_t i = 0; i < fatalSignals.size(); ++i)
        {
            if (fatalSignals[i] == sig && replaced[i])
            {
                sigaction (sig, &previous[i], nullptr);
                return (previous[i].sa_flags & SA_SIGINFO) == 0 && previous[i].sa_handler == SIG_DFL;
            }
        }

        return true;
    }

    void onSignal (int sig, siginfo_t* info, void*)
    {
        void* frames[64];
        const auto count = backtrace (frames, 64);

        // The first frames are this handler's (ours) and the system's signal trampoline;
        // the crash is ours if this module is on the stack below them.
        int first = 0;
        while (first < count && isOurs (reinterpret_cast<std::uintptr_t> (frames[first])))
            ++first;

        bool ours = false;
        for (int i = first; i < count && ! ours; ++i)
            ours = isOurs (reinterpret_cast<std::uintptr_t> (frames[i]));

        if (ours && ! writing.exchange (true))
        {
            char path[pathLength + 40];
            reportPath (path, sizeof (path));

            Writer out;
            out.file = ::open (path, O_WRONLY | O_CREAT | O_EXCL, 0600);

            if (out.file >= 0)
            {
                out << "Drenched crashed: " << signalName (sig);
                if (info != nullptr && sig != SIGABRT)
                {
                    out << ", address ";
                    out.hex (reinterpret_cast<std::uintptr_t> (info->si_addr));
                }
                out << "\n" << aboutText.data() << "\n\nStack (module+offset):\n";

                for (int i = first; i < count; ++i)
                {
                    out << " ";
                    out.number (static_cast<std::uint64_t> (i - first)) << " ";
                    writeFrame (out, frames[i]);
                    out << "\n";
                }

                writeSteps (out);
                out.flush();
                ::close (out.file);
            }
        }

        // Whatever was there before handles it now: a fault happens again when this
        // returns, an abort raises again, and the host's crash reporter sees it.
        const bool ends = putBack (sig);

        // With nothing else there, the default action ends the process now: sent again,
        // it does, however the signal came. Otherwise a signal sent rather than caused
        // (abort(), raise(), kill) doesn't happen again by itself, so it is sent again,
        // and a fault happens again when this returns, so the handler there sees the real
        // fault. A fault the processor raised carries one of the small positive codes
        // (SEGV_MAPERR, BUS_ADRERR, ILL_ILLOPC, FPE_INTDIV...); a sent signal carries 0
        // or below on Linux (SI_USER, SI_TKILL) and 0 or 0x10001 and up on macOS.
        const auto code = info != nullptr ? info->si_code : 0;
        const bool caused = sig != SIGABRT && code > 0 && code < 0x100;

        if (ends || ! caused)
            std::raise (sig);
    }

    void installHandlers() noexcept
    {
        Dl_info info {};
        if (dladdr (reinterpret_cast<void*> (&installHandlers), &info) == 0 || info.dli_fbase == nullptr)
            return;

        moduleBase = info.dli_fbase;
        processId = static_cast<std::uint64_t> (getpid());

        // Kept loaded from now on, so the handler is never left pointing at unloaded code:
        // another Drenched binary (VST3 and CLAP in one host), or anything else that chains
        // to it, may hand crashes on to it after this one's instances have all gone.
        dlopen (info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);

        // The first backtrace() loads the unwinder, which allocates; done here, so the
        // handler doesn't do it inside a crash in malloc.
        void* warm[1];
        backtrace (warm, 1);

        for (size_t i = 0; i < fatalSignals.size(); ++i)
        {
            struct sigaction action {};
            action.sa_sigaction = onSignal;
            // On the alternate stack when the thread has one: a host's handler set up for
            // stack overflows only runs if this one, which hands crashes on to it, does too.
            action.sa_flags = SA_SIGINFO | SA_ONSTACK;
            sigemptyset (&action.sa_mask);
            replaced[i] = sigaction (fatalSignals[i], &action, &previous[i]) == 0;
        }
    }

   #endif

   #if JUCE_WINDOWS
    // Takes the handlers away when the module unloads, so nothing calls into unloaded code.
    // (On macOS and Linux the module stays loaded instead: see installHandlers.)
    struct Remover
    {
        ~Remover() { if (installed.load()) removeHandlers(); }
    };

    Remover remover;
   #endif
}

//==============================================================================
void install (const juce::File& folder, const juce::String& about)
{
    if (installed.exchange (true))
        return;

    folder.createDirectory();
    const auto path = folder.getFullPathName() + juce::File::getSeparatorString();
    copyText (folderPath.data(), folderPath.size(), path.toRawUTF8());
    copyText (aboutText.data(), aboutText.size(), about.toRawUTF8());

    // Old reports beyond the newest few aren't worth keeping.
    auto files = folder.findChildFiles (juce::File::findFiles, false, "crash-*.txt");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    for (int i = 5; i < files.size(); ++i)
        files.getReference (i).deleteFile();

    installHandlers();
}

bool isInstalled()
{
    return installed.load();
}

void step (const juce::String& what)
{
    const std::lock_guard lock (stepsLock);
    const auto index = nextStep.fetch_add (1) % stepCount;
    auto& s = steps[index];
    s.when.store (0);
    copyText (s.text.data(), s.text.size(), what.replaceCharacters ("\r\n", "  ").toRawUTF8());
    s.when.store (juce::Time::currentTimeMillis());
}

juce::String recentSteps()
{
    const std::lock_guard lock (stepsLock);
    juce::StringArray lines;
    const auto end = nextStep.load();

    for (std::uint32_t i = end > stepCount ? end - stepCount : 0; i < end; ++i)
    {
        const auto& s = steps[i % stepCount];
        const auto when = s.when.load();
        if (when == 0)
            continue;
        lines.add (juce::Time (when).formatted ("%H:%M:%S") + " " + juce::String::fromUTF8 (s.text.data()));
    }

    return lines.joinIntoString ("\n");
}

std::optional<Crash> pending (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "crash-*.txt");

    std::optional<Crash> newest;

    for (const auto& file : files)
    {
        const auto when = juce::Time (1000 * file.getFileNameWithoutExtension().fromFirstOccurrenceOf ("crash-", false, false).getLargeIntValue());

        if (newest.has_value() && when <= newest->when)
            continue;

        auto text = file.loadFileAsString();

        if (text.isNotEmpty())
            newest = Crash { file, text.substring (0, 48000), when };
    }

    return newest;
}

void handled (const juce::File& crashFile)
{
    crashFile.deleteFile();
}

} // namespace drenched::crashReports
