// include/trace/Trace.h
#pragma once
#include <cstdint>
#include <cstdio>
#include <atomic>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

namespace trace {

enum class Origin : uint8_t {
    Internal = 0, User, Device, Signal, Timer, Network
};

struct Context {
    uint64_t    trace_id  = 0;
    uint64_t    parent_id = 0;
    Origin      origin    = Origin::Internal;
    const char* source    = "";
    const char* thread    = "";
};

inline thread_local Context t_ctx{};

inline uint64_t nextId() {
    static std::atomic<uint64_t> c{1};
    return c.fetch_add(1, std::memory_order_relaxed);
}

inline const char* threadName() {
    static thread_local char buf[32] = {0};
    if (buf[0]) return buf;
#if defined(_WIN32)
    std::snprintf(buf, sizeof(buf), "%lu", (unsigned long)GetCurrentThreadId());
#else
    std::snprintf(buf, sizeof(buf), "%lu", (unsigned long)pthread_self());
#endif
    return buf;
}

inline uint64_t nowNs() {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

inline void emitJsonString(std::FILE* f, const char* s) {
    std::fputc('"', f);
    if (s) {
        for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
            switch (*p) {
            case '"':  std::fputs("\\\"", f); break;
            case '\\': std::fputs("\\\\", f); break;
            case '\b': std::fputs("\\b",  f); break;
            case '\f': std::fputs("\\f",  f); break;
            case '\n': std::fputs("\\n",  f); break;
            case '\r': std::fputs("\\r",  f); break;
            case '\t': std::fputs("\\t",  f); break;
            default:
                if (*p < 0x20) std::fprintf(f, "\\u%04x", *p);
                else           std::fputc(*p, f);
            }
        }
    }
    std::fputc('"', f);
}

inline void emitLog(const char* ev, uint64_t id, uint64_t parent,
                    uint64_t trace_id, const Context& c,
                    const char* cls, const char* fn,
                    const char* file, int line) {
    std::FILE* f = stderr;

    std::fputs("{\"ev\":", f);
    emitJsonString(f, ev);

    std::fprintf(f,
                 ",\"id\":%llu,\"parent\":%llu,\"trace\":%llu,\"origin\":%d",
                 (unsigned long long)id,
                 (unsigned long long)parent,
                 (unsigned long long)trace_id,
                 (int)c.origin);

    std::fputs(",\"src\":", f);
    emitJsonString(f, c.source);

    std::fputs(",\"thread\":", f);
    emitJsonString(f, threadName());

    std::fprintf(f, ",\"ts\":%llu", (unsigned long long)nowNs());

    std::fputs(",\"cls\":", f);
    emitJsonString(f, cls ? cls : "");

    std::fputs(",\"fn\":", f);
    emitJsonString(f, fn ? fn : "");

    std::fputs(",\"file\":", f);
    emitJsonString(f, file ? file : "");

    std::fprintf(f, ",\"line\":%d}\n", line);
}

class Guard {
public:
    Guard(const char* cls, const char* fn, const char* file, int line)
        : prev_(t_ctx), id_(nextId())
    {
        const uint64_t parent  = prev_.parent_id;
        const uint64_t traceId = prev_.trace_id ? prev_.trace_id : id_;
        emitLog("enter", id_, parent, traceId, t_ctx, cls, fn, file, line);
        t_ctx.parent_id = id_;
        t_ctx.trace_id  = traceId;
    }

    ~Guard() {
        emitLog("exit", id_, prev_.parent_id,
                prev_.trace_id ? prev_.trace_id : id_,
                prev_, nullptr, nullptr, nullptr, 0);
        t_ctx = prev_;
    }

private:
    Context  prev_;
    uint64_t id_;
};

class OriginScope {
public:
    OriginScope(Origin o, const char* src)
        : prev_(t_ctx) {
        t_ctx.origin    = o;
        t_ctx.source    = src;
        t_ctx.trace_id  = nextId();
        t_ctx.parent_id = 0;
    }
    ~OriginScope() { t_ctx = prev_; }

private:
    Context prev_;
};

} // namespace trace

#if defined(__GNUC__) || defined(__clang__)
#  define TRACE_CLASS __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#  define TRACE_CLASS __FUNCSIG__
#else
#  define TRACE_CLASS __func__
#endif

#define TRACE_FN() \
    ::trace::Guard _ronia_trace_guard_(TRACE_CLASS, __func__, __FILE__, __LINE__)
