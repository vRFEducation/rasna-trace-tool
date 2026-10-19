#pragma once
#include <cstdint>
#include <string>

namespace rt {

struct Event {
    std::string ev;
    uint64_t    id      = 0;
    uint64_t    parent  = 0;
    uint64_t    trace   = 0;
    int         origin  = 0;
    std::string src;
    std::string thread;
    uint64_t    ts      = 0;
    std::string cls;
    std::string fn;
    std::string file;
    int         line    = 0;

    // filled at load time
    int      parent_index = -1;
    int      first_child  = -1;
    int      next_sibling = -1;
    uint64_t dur_ns       = 0;
    int      depth        = 0;

    bool isEnter() const { return ev == "enter"; }
    bool isExit()  const { return ev == "exit";  }
};

} // namespace rt