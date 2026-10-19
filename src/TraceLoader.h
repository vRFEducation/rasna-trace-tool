#pragma once
#include "Event.h"
#include <string>
#include <vector>

namespace rt {

struct LoadStats {
    size_t   total       = 0;
    size_t   enters      = 0;
    size_t   exits       = 0;
    size_t   orphanExits = 0;
    size_t   dropped     = 0;
    size_t   threads     = 0;
    size_t   traces      = 0;
    uint64_t first_ts    = 0;
    uint64_t last_ts     = 0;
};

struct Trace {
    std::vector<Event> events;
    std::vector<int>   roots;
    LoadStats          stats;
};

bool loadTrace(const std::string& path, Trace& out, std::string& err);

} // namespace rt