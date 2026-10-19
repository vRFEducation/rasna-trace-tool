// include/trace/Trace.h
#pragma once
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <string>


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
// ---------------------------------------------------------------------------
// sink: env-var-controlled output file (falls back to stderr)
// ---------------------------------------------------------------------------
inline std::FILE* traceSink() {
    static std::FILE* f = []() -> std::FILE* {
        const char* p = std::getenv("RONIA_TRACE");
        if (!p || !*p) return stderr;
        const char* trunc = std::getenv("RONIA_TRACE_TRUNC");
        const bool  doTrunc = (trunc && *trunc && *trunc != '0');
        std::FILE* fp = std::fopen(p, doTrunc ? "wb" : "ab");
        if (!fp) {
            std::fprintf(stderr, "[trace] could not open %s, using stderr\n", p);
            return stderr;
        }
        return fp;
    }();
    return f;
}
// ---------------------------------------------------------------------------
// monotonic ns (frequency cached on Windows)
// ---------------------------------------------------------------------------
inline uint64_t nowNs() {
#if defined(_WIN32)
    static const double freq = []() {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        return (double)f.QuadPart;
    }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / freq);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

// ---------------------------------------------------------------------------
// JSON string escaper (you already added this — keep it, just placing here)
// ---------------------------------------------------------------------------
inline void emitJsonString(std::string& out, const char* s) {
    out.push_back('"');
    if (s) {
        for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
            switch (*p) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (*p < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", *p);
                        out += buf;
                    } else {
                        out.push_back((char)*p);
                    }
            }
        }
    }
    out.push_back('"');
}

// ---------------------------------------------------------------------------
// emitLog: build one record, then a single fwrite
// ---------------------------------------------------------------------------
inline void emitLog(const char* ev, uint64_t id, uint64_t parent,
                    uint64_t trace_id, const Context& c,
                    const char* cls, const char* fn,
                    const char* file, int line) {
    thread_local std::string buf;
    buf.clear();
    buf.reserve(512);

    auto appendU64 = [&](const char* key, uint64_t v) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), ",\"%s\":%llu", key, (unsigned long long)v);
        buf += tmp;
    };

    buf += "{\"ev\":";
    emitJsonString(buf, ev);

    appendU64("id",     id);
    appendU64("parent", parent);
    appendU64("trace",  trace_id);

    {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), ",\"origin\":%d", (int)c.origin);
        buf += tmp;
    }

    buf += ",\"src\":";
    emitJsonString(buf, c.source);

    buf += ",\"thread\":";
    emitJsonString(buf, threadName());

    appendU64("ts", nowNs());

    buf += ",\"cls\":";
    emitJsonString(buf, cls ? cls : "");

    buf += ",\"fn\":";
    emitJsonString(buf, fn ? fn : "");

    buf += ",\"file\":";
    emitJsonString(buf, file ? file : "");

    {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), ",\"line\":%d}\n", line);
        buf += tmp;
    }

    std::FILE* f = traceSink();
    std::fwrite(buf.data(), 1, buf.size(), f);
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
