#include "TraceLoader.h"
#include "third_party/nlohmann/json.hpp"

#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace rt {

using nlohmann::json;

static bool parseLine(const std::string& line, Event& e) {
    try {
        json j = json::parse(line);
        e.ev     = j.value("ev",     std::string());
        e.id     = j.value("id",     0ULL);
        e.parent = j.value("parent", 0ULL);
        e.trace  = j.value("trace",  0ULL);
        e.origin = j.value("origin", 0);
        e.src    = j.value("src",    std::string());
        e.thread = j.value("thread", std::string());
        e.ts     = j.value("ts",     0ULL);
        e.cls    = j.value("cls",    std::string());
        e.fn     = j.value("fn",     std::string());
        e.file   = j.value("file",   std::string());
        e.line   = j.value("line",   0);
        return e.isEnter() || e.isExit();
    } catch (const std::exception&) {
        return false;
    }
}

bool loadTrace(const std::string& path, Trace& out, std::string& err) {
    out = Trace{};
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path; return false; }

    std::string line;
    line.reserve(1024);

    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        size_t s = 0;
        while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
        if (s >= line.size() || line[s] != '{') continue;

        Event e;
        if (!parseLine(line.substr(s), e)) { ++out.stats.dropped; continue; }
        out.events.push_back(std::move(e));
    }

    out.stats.total = out.events.size();
    if (out.events.empty()) { err = "no events parsed"; return false; }

    std::unordered_map<std::string, std::vector<int>> stack;
    std::unordered_set<std::string> threads_seen;
    std::unordered_set<uint64_t>    traces_seen;

    for (size_t i = 0; i < out.events.size(); ++i) {
        Event& e = out.events[i];

        if (e.isEnter()) {
            ++out.stats.enters;
            auto& st = stack[e.thread];
            if (!st.empty()) {
                int p = st.back();
                e.parent_index = p;
                e.depth        = out.events[p].depth + 1;
                if (out.events[p].first_child < 0) {
                    out.events[p].first_child = (int)i;
                } else {
                    int c = out.events[p].first_child;
                    while (out.events[c].next_sibling >= 0)
                        c = out.events[c].next_sibling;
                    out.events[c].next_sibling = (int)i;
                }
            } else {
                out.roots.push_back((int)i);
            }
            st.push_back((int)i);

            threads_seen.insert(e.thread);
            traces_seen.insert(e.trace);
            if (out.stats.first_ts == 0 || e.ts < out.stats.first_ts)
                out.stats.first_ts = e.ts;
            if (e.ts > out.stats.last_ts) out.stats.last_ts = e.ts;
        } else {
            ++out.stats.exits;
            auto it = stack.find(e.thread);
            if (it != stack.end() && !it->second.empty()) {
                int opener = it->second.back();
                if (out.events[opener].id == e.id) {
                    out.events[opener].dur_ns = e.ts - out.events[opener].ts;
                    e.parent_index            = opener;
                    e.dur_ns                  = out.events[opener].dur_ns;
                    e.depth                   = out.events[opener].depth;
                    it->second.pop_back();
                } else {
                    ++out.stats.orphanExits;
                }
            } else {
                ++out.stats.orphanExits;
            }
            if (e.ts > out.stats.last_ts) out.stats.last_ts = e.ts;
        }
    }

    out.stats.threads = threads_seen.size();
    out.stats.traces  = traces_seen.size();
    return true;
}

} // namespace rt