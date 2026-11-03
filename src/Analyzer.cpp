#include "Analyzer.h"
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <algorithm>

namespace rt {

static QString layerOf(const Event& e) {
    QString f = QString::fromStdString(e.file).toLower();
    f.replace('\\', '/');
    if (f.contains("/ui/"))       return "ui";
    if (f.contains("/app/"))      return "app";
    if (f.contains("/services/")) return "services";
    if (f.contains("/infra/"))    return "infra";
    if (f.contains("/domain/"))   return "domain";
    return "?";
}

static int layerRank(const QString& l) {
    // lower number = lower layer
    if (l == "domain")   return 0;
    if (l == "infra")    return 1;
    if (l == "services") return 2;
    if (l == "app")      return 3;
    if (l == "ui")       return 4;
    return 5;
}

static QString detectGuiThread(const Trace& t) {
    // The thread that ran the most ui-layer functions wins.
    QHash<QString, int> score;
    for (const auto& e : t.events) {
        if (!e.isEnter()) continue;
        if (layerOf(e) == "ui") score[QString::fromStdString(e.thread)]++;
    }
    QString best; int bestv = 0;
    for (auto it = score.begin(); it != score.end(); ++it)
        if (it.value() > bestv) { bestv = it.value(); best = it.key(); }
    return best;
}

Analysis analyze(const Trace& trace, const AnalyzeOptions& optsIn) {
    Analysis a;
    AnalyzeOptions opts = optsIn;
    if (opts.guiThread.isEmpty()) opts.guiThread = detectGuiThread(trace);

    // ---- 1. layer violations ----
    {
        QHash<QString, LayerViolation> agg;  // key = caller->callee|via
        for (size_t i = 0; i < trace.events.size(); ++i) {
            const Event& e = trace.events[i];
            if (!e.isEnter()) continue;
            if (e.parent_index < 0) continue;
            const Event& p = trace.events[e.parent_index];
            QString pc = layerOf(p);
            QString cc = layerOf(e);
            if (pc == "?" || cc == "?" || pc == cc) continue;
            if (layerRank(pc) < layerRank(cc)) {
                QString key = pc + "->" + cc + "|" + QString::fromStdString(e.fn);
                auto& v = agg[key];
                if (v.count == 0) {
                    v.callerLayer = pc;
                    v.calleeLayer = cc;
                    v.via = QString::fromStdString(e.fn);
                    v.sampleEvent = (int)i;
                }
                v.count++;
            }
        }
        a.violations.assign(agg.begin(), agg.end());
        std::sort(a.violations.begin(), a.violations.end(),
                  [](const LayerViolation& x, const LayerViolation& y){
                      return x.count > y.count;
                  });
    }

    // ---- 2. GUI-thread blocking ----
    {
        for (size_t i = 0; i < trace.events.size(); ++i) {
            const Event& e = trace.events[i];
            if (!e.isEnter()) continue;
            if (QString::fromStdString(e.thread) != opts.guiThread) continue;
            if (e.dur_ns < opts.blockingThresholdNs) continue;

            // Only count as blocking if no child of this call also qualified
            // on the same thread — i.e. this is the outermost slow call.
            bool hasSlowChild = false;
            int c = e.first_child;
            while (c >= 0) {
                const Event& ch = trace.events[c];
                if (ch.dur_ns >= opts.blockingThresholdNs) { hasSlowChild = true; break; }
                c = ch.next_sibling;
            }
            if (hasSlowChild) continue;

            BlockingFinding b;
            b.layer   = layerOf(e);
            b.fn      = QString::fromStdString(e.fn);
            b.cls     = QString::fromStdString(e.cls);
            b.file    = QString::fromStdString(e.file);
            b.line    = e.line;
            b.thread  = QString::fromStdString(e.thread);
            b.dur_ns  = e.dur_ns;
            b.eventIndex = (int)i;
            a.blocking.push_back(b);
            a.totalBlockedNs += e.dur_ns;
        }
        std::sort(a.blocking.begin(), a.blocking.end(),
                  [](const BlockingFinding& x, const BlockingFinding& y){
                      return x.dur_ns > y.dur_ns;
                  });
    }

    // ---- 3. fan-in ----
    {
        QHash<QString, QSet<QString>> parents;   // fn -> set of parent fns
        QHash<QString, int> calls;
        for (size_t i = 0; i < trace.events.size(); ++i) {
            const Event& e = trace.events[i];
            if (!e.isEnter()) continue;
            QString fn = QString::fromStdString(e.fn);
            calls[fn]++;
            if (e.parent_index >= 0) {
                QString pfn = QString::fromStdString(trace.events[e.parent_index].fn);
                parents[fn].insert(pfn);
            }
        }
        for (auto it = parents.begin(); it != parents.end(); ++it) {
            FanInFinding f;
            f.fn = it.key();
            f.distinctParents = it.value().size();
            f.calls = calls.value(f.fn);
            a.fanIn.push_back(f);
        }
        std::sort(a.fanIn.begin(), a.fanIn.end(),
                  [](const FanInFinding& x, const FanInFinding& y){
                      return x.distinctParents > y.distinctParents;
                  });
    }

    // ---- 4. temporal coupling (adjacent siblings under a common parent) ----
    {
        QHash<QString, QSet<QString>> parentGroups;      // pair -> set of parent keys
        QHash<QString, int>           pairTotals;        // pair -> total occurrences
        QHash<QString, QSet<QString>> parentsWithFirst;  // fn  -> set of parent keys

        for (size_t i = 0; i < trace.events.size(); ++i) {
            const Event& p = trace.events[i];
            if (!p.isEnter()) continue;

            QVector<QString> kids;
            int c = p.first_child;
            while (c >= 0) {
                kids.push_back(QString::fromStdString(trace.events[c].fn));
                c = trace.events[c].next_sibling;
            }
            if (kids.isEmpty()) continue;

            QString pk = QString::number((qulonglong)p.id) + "@" +
                         QString::fromStdString(p.thread);

            for (const auto& kn : kids) parentsWithFirst[kn].insert(pk);

            for (int k = 0; k + 1 < kids.size(); ++k) {
                QString key = kids[k] + " -> " + kids[k+1];
                parentGroups[key].insert(pk);
                pairTotals[key]++;
            }
        }

        for (auto it = pairTotals.begin(); it != pairTotals.end(); ++it) {
            CouplingFinding f;
            const QString& key = it.key();
            int arrow = key.indexOf(" -> ");
            f.first  = key.left(arrow);
            f.second = key.mid(arrow + 4);
            f.occurrences = parentGroups[key].size();
            f.totalGroups = parentsWithFirst.value(f.first).size();
            f.ratio = f.totalGroups ? double(f.occurrences) / f.totalGroups : 0.0;
            if (f.occurrences >= 2) a.coupling.push_back(f);
        }
        std::sort(a.coupling.begin(), a.coupling.end(),
                  [](const CouplingFinding& x, const CouplingFinding& y){
                      return x.ratio != y.ratio ? x.ratio > y.ratio
                                                : x.occurrences > y.occurrences;
                  });
    }   
    // ---- 5. entry points (roots) ----
    {
        QHash<QString, int> counts;
        for (int ri : trace.roots) {
            counts[QString::fromStdString(trace.events[ri].fn)]++;
        }
        for (auto it = counts.begin(); it != counts.end(); ++it) {
            EntryPointFinding f;
            f.fn = it.key();
            f.count = it.value();
            a.entries.push_back(f);
        }
        std::sort(a.entries.begin(), a.entries.end(),
                  [](const EntryPointFinding& x, const EntryPointFinding& y){
                      return x.count > y.count;
                  });
    }

    return a;
}

} // namespace rt