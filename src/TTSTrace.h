#pragma once

// Shared earliest-possible diagnostic trace.
//
// Deliberately depends on nothing but Win32/CRT -- no spdlog, no F4SE, no TTS
// component -- so it still works when static initialisation or a hook callback
// is what is killing us. It writes straight through the Win32 API (and, as a
// fallback, the CRT) so that whichever path happens to be hooked, the other may
// still get through.
//
// Used by main.cpp for the load/init sequence and by D3DHooks.cpp for the
// render-path hooks: when Buffout reports only "TrueThroughScope.dll+0014169",
// these lines are what narrow it down to a specific call.

#define TTS_TRACE_CANDIDATES \
    "E:\\Custom Fallout4\\tts-trace.log", \
    "E:\\tts-trace.log", \
    "D:\\dsh\\bld\\tts-trace.log", \
    "C:\\Users\\SRVERGIL\\tts-trace.log"

inline void TTS_TraceWrite(const char* a_msg)
{
    static const char* kPaths[] = { TTS_TRACE_CANDIDATES };

    for (int i = 0; i < 4; ++i) {
        // 1) Win32
        HANDLE h = CreateFileA(kPaths[i], GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            SetFilePointer(h, 0, NULL, FILE_END);
            DWORD w = 0;
            WriteFile(h, a_msg, (DWORD)strlen(a_msg), &w, NULL);
            WriteFile(h, "\r\n", 2, &w, NULL);
            CloseHandle(h);
            return;
        }
        // 2) CRT -- different API path, may bypass a Win32-only hook
#pragma warning(push)
#pragma warning(disable : 4996)   // fopen: SDL deprecation warning, deliberate here
        FILE* f = fopen(kPaths[i], "ab");
#pragma warning(pop)
        if (f) {
            fwrite(a_msg, 1, strlen(a_msg), f);
            fwrite("\r\n", 1, 2, f);
            fclose(f);
            return;
        }
    }
}

#define TTS_TRACE(msg) TTS_TraceWrite(msg)
