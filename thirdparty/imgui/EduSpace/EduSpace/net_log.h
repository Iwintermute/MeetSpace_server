#pragma once
#include <chrono>
#include <cstdio>
#include <thread>

namespace netlog {

    inline uint64_t msNow() {
        using namespace std::chrono;
        return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    inline const char* tid() {
        static thread_local char buf[32];
        std::snprintf(buf, sizeof(buf), "%zu", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        return buf;
    }

    inline void info(const char* tag, const char* fmt, ...) {
        std::printf("[%llu][%s][%s] ", (unsigned long long)msNow(), tid(), tag);
        va_list ap; va_start(ap, fmt);
        std::vprintf(fmt, ap);
        va_end(ap);
        std::printf("\n");
    }

    inline void err(const char* tag, const char* fmt, ...) {
        std::fprintf(stderr, "[%llu][%s][%s][ERR] ", (unsigned long long)msNow(), tid(), tag);
        va_list ap; va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        std::fprintf(stderr, "\n");
    }

} // namespace netlog
