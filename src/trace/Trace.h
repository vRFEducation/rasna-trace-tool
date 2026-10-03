// trace/Trace.h
#pragma once
#include <cstdint>
#include <cstdio>
#include <atomic>

namespace trace {

enum class Origin : uint8_t {
    Internal = 0, User, Device, Signal, Timer, Network
};

struct Context {
    uint64_t    trace_id  = 0;
    uint64_t    parent_id = 0;   // id of the enclosing event, if any
    Origin      origin    = Origin::Internal;
    const char* source    = "";
    const char* thread    = "";
};

inline thread_local Context t_ctx{};

inline uint64_t nextId() {
    static std::atomic<uint64_t> c{1};
    return c.fetch_add(1, std::memory_order_relaxed);
}

// ---- log sink: replace with your real logger ----
inline void emitLog(const char* ev, uint64_t id,
                    const Context& c,
                    const char* cls, const char* fn,
                    const char* file, int line) {
    std::fprintf(stderr,
        R"({"ev":"%s","id":%llu,"parent":%llu,"trace":%llu,)"
        R"("origin":%d,"src":"%s","thread":"%s",)"
        R"("cls":"%s","fn":"%s","file":"%s","line":%d})" "\n",
        ev,
        (unsigned long long)id,
        (unsigned long long)c.parent_id,
        (unsigned long long)c.trace_id,
        (int)c.origin, c.source, c.thread,
        cls ? cls : "", fn ? fn : "", file ? file : "", line);
}

class Guard {
public:
    Guard(const char* cls, const char* fn, const char* file, int line)
        : prev_(t_ctx),
          id_(nextId())
    {
        if (t_ctx.trace_id == 0) t_ctx.trace_id = id_;
        t_ctx.parent_id = id_;            // becomes parent for nested calls
        emitLog("enter", id_, t_ctx, cls, fn, file, line);
    }

    ~Guard() {
        emitLog("exit", id_, t_ctx, nullptr, nullptr, nullptr, 0);
        t_ctx = prev_;                    // restore parent_id for the caller
    }

private:
    Context  prev_;
    uint64_t id_;
};

class OriginScope {
public:
    OriginScope(Origin o, const char* src)
        : prev_(t_ctx)
    {
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

// ---- convenience macro ----
#if defined(__GNUC__) || defined(__clang__)
#  define TRACE_CLASS __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#  define TRACE_CLASS __FUNCSIG__
#else
#  define TRACE_CLASS __func__
#endif

#define TRACE_FN() \
    ::trace::Guard _ronia_trace_guard_(TRACE_CLASS, __func__, __FILE__, __LINE__)