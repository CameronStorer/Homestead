#pragma once

// Debug trace for hunting hangs on real hardware: each call appends one line
// to sdmc:/3ds/homestead/trace.log and closes the file, so the last line
// survives a freeze and a forced power-off. Compiled out unless
// HOMESTEAD_TRACE is defined (make HOMESTEAD_TRACE=1).

#ifdef HOMESTEAD_TRACE
#include <3ds.h>
#include <cstdarg>
#include <cstdio>

namespace Homestead {
    inline void trace(const char *fmt, ...) {
        FILE *f = std::fopen("sdmc:/3ds/homestead/trace.log", "a");
        if (!f) return;
        std::fprintf(f, "%llu ", (unsigned long long)osGetTime());
        va_list args;
        va_start(args, fmt);
        std::vfprintf(f, fmt, args);
        va_end(args);
        std::fputc('\n', f);
        std::fclose(f);
    }
}
#define HS_TRACE(...) ::Homestead::trace(__VA_ARGS__)
#else
#define HS_TRACE(...) ((void)0)
#endif
