#include "ReportWriter.h"
#include "TraceModel.h"          // for layerOf — or duplicate it below
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <QHash>

namespace rt {

// ---- small helpers ---------------------------------------------------------

static QString htmlEscape(const QString& s) {
    QString o = s;
    o.replace('&', "&amp;")
     .replace('<', "&lt;")
     .replace('>', "&gt;")
     .replace('"', "&quot;");
    return o;
}

static QString layerOfFile(const QString& file) {
    QString f = file.toLower(); f.replace('\\', '/');
    if (f.contains("/ui/"))       return "ui";
    if (f.contains("/app/"))      return "app";
    if (f.contains("/services/")) return "services";
    if (f.contains("/infra/"))    return "infra";
    if (f.contains("/domain/"))   return "domain";
    return "?";
}

static QString layerColor(const QString& l) {
    if (l == "ui")       return "#4a90d9";
    if (l == "app")      return "#50b06a";
    if (l == "services") return "#d9a84a";
    if (l == "infra")    return "#d96a4a";
    if (l == "domain")   return "#9a9a9a";
    return "#888888";
}

static QString ms(uint64_t ns, int decimals = 3) {
    return QString::number(ns / 1e6, 'f', decimals);
}

// ---- table helpers ---------------------------------------------------------

struct Col { QString title; QString align; };

static QString tableHeader(const QVector<Col>& cols) {
    QString out = "<thead><tr>";
    for (const auto& c : cols)
        out += QString("<th style=\"text-align:%1\">%2</th>")
                   .arg(c.align.isEmpty() ? "left" : c.align, htmlEscape(c.title));
    out += "</tr></thead>";
    return out;
}

// ---- SVG timeline ----------------------------------------------------------

static QString buildTimelineSvg(const Trace& t, int width = 1200) {
    // one row per thread, colored bars per enter event
    QHash<QString, QVector<int>> byThread;
    for (size_t i = 0; i < t.events.size(); ++i) {
        const Event& e = t.events[i];
        if (!e.isEnter()) continue;
        byThread[QString::fromStdString(e.thread)].push_back((int)i);
    }
    QStringList threads = byThread.keys();
    std::sort(threads.begin(), threads.end());

    const int rowH    = 18;
    const int labelW  = 100;
    const int headerH = 20;
    const int height  = headerH + rowH * threads.size();

    uint64_t t0 = t.stats.first_ts, t1 = t.stats.last_ts;
    if (t1 == t0) t1 = t0 + 1;

    auto xFor = [&](uint64_t ts, int plotW) {
        return labelW + (double(ts - t0) / double(t1 - t0)) * plotW;
    };
    const int plotW = width - labelW;

    QString s;
    s += QString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\" "
                 "viewBox=\"0 0 %1 %2\" style=\"background:#1e1e1e;font-family:sans-serif;font-size:11px\">")
             .arg(width).arg(height);

    // ruler
    for (int i = 0; i <= 10; ++i) {
        int x = labelW + plotW * i / 10;
        double offsetMs = double(t1 - t0) * i / 10 / 1e6;
        s += QString("<line x1=\"%1\" y1=\"0\" x2=\"%1\" y2=\"%2\" stroke=\"#444\"/>")
                 .arg(x).arg(height);
        s += QString("<text x=\"%1\" y=\"12\" fill=\"#888\">%2 ms</text>")
                 .arg(x + 2).arg(offsetMs, 0, 'f', 1);
    }

    for (int r = 0; r < threads.size(); ++r) {
        const QString& th = threads[r];
        const int y = headerH + r * rowH;
        s += QString("<rect x=\"0\" y=\"%1\" width=\"%2\" height=\"%3\" fill=\"#2a2a2a\"/>")
                 .arg(y).arg(width).arg(rowH - 1);
        s += QString("<text x=\"4\" y=\"%1\" fill=\"#ddd\">t=%2</text>")
                 .arg(y + 13).arg(htmlEscape(th));

        for (int eIdx : byThread[th]) {
            const Event& e = t.events[eIdx];
            double x0 = xFor(e.ts,             plotW);
            double x1 = xFor(e.ts + e.dur_ns,  plotW);
            if (x1 - x0 < 0.6) x1 = x0 + 0.6;
            const QString layer = layerOfFile(QString::fromStdString(e.file));
            const QString color = layerColor(layer);
            QString tip = QString("%1\n%2 ms\n%3:%4")
                              .arg(QString::fromStdString(e.fn))
                              .arg(e.dur_ns / 1e6, 0, 'f', 3)
                              .arg(QString::fromStdString(e.file))
                              .arg(e.line);
            s += QString("<rect x=\"%1\" y=\"%2\" width=\"%3\" height=\"%4\" "
                         "fill=\"%5\"><title>%6</title></rect>")
                     .arg(x0, 0, 'f', 2)
                     .arg(y + 2)
                     .arg(x1 - x0, 0, 'f', 2)
                     .arg(rowH - 6)
                     .arg(color)
                     .arg(htmlEscape(tip));
        }
    }
    s += "</svg>";
    return s;
}

// ---- call tree as nested <details> -----------------------------------------

static QString buildTreeHtml(const Trace& t, int eventIndex, int maxDepth, int depth) {
    if (eventIndex < 0 || eventIndex >= (int)t.events.size()) return {};
    const Event& e = t.events[eventIndex];

    QString kids;
    int c = e.first_child;
    bool any = false;
    while (c >= 0) { any = true;
        kids += buildTreeHtml(t, c, maxDepth, depth + 1);
        c = t.events[c].next_sibling;
    }

    const QString layer = layerOfFile(QString::fromStdString(e.file));
    const QString color = layerColor(layer);
    const QString label = QString("%1  [%2]  %3 ms  (%4:%5)")
        .arg(htmlEscape(QString::fromStdString(e.fn)))
        .arg(layer)
        .arg(ms(e.dur_ns))
        .arg(htmlEscape(QFileInfo(QString::fromStdString(e.file)).fileName()))
        .arg(e.line);

    if (!any) {
        return QString("<li><span class=\"leaf\" style=\"border-left-color:%1\">%2</span></li>")
                   .arg(color, label);
    }

    const bool open = depth < 2;
    return QString(
        "<li>"
        "<details %1>"
        "<summary style=\"border-left-color:%2\">%3</summary>"
        "<ul>%4</ul>"
        "</details>"
        "</li>")
        .arg(open ? "open" : "")
        .arg(color)
        .arg(label)
        .arg(kids);
}

// ---- main entry ------------------------------------------------------------

bool writeHtmlReport(const QString& path,
                     const Trace& t,
                     const Analysis& a,
                     const ReportOptions& opts,
                     QString* errOut)
{
    auto fail = [&](const QString& msg){
        if (errOut) *errOut = msg;
        return false;
    };

    const auto& s = t.stats;
    const double spanMs = (s.last_ts > s.first_ts)
        ? double(s.last_ts - s.first_ts) / 1e6 : 0.0;

    // --- top functions aggregation (same as AnalysisTabs) ---
    struct Agg { QString fn; QString layer; int calls = 0;
                 uint64_t total = 0, maxv = 0; };
    QHash<QString, Agg> byFn;
    for (const auto& e : t.events) {
        if (!e.isEnter()) continue;
        QString fn = QString::fromStdString(e.fn);
        auto& x = byFn[fn];
        if (x.calls == 0) { x.fn = fn;
                            x.layer = layerOfFile(QString::fromStdString(e.file)); }
        x.calls++;
        x.total += e.dur_ns;
        if (e.dur_ns > x.maxv) x.maxv = e.dur_ns;
    }
    QVector<Agg> tops;
    tops.reserve(byFn.size());
    for (auto it = byFn.begin(); it != byFn.end(); ++it) tops.push_back(it.value());
    std::sort(tops.begin(), tops.end(),
              [](const Agg& x, const Agg& y){ return x.total > y.total; });
    if (tops.size() > opts.topFunctionLimit) tops.resize(opts.topFunctionLimit);

    // --- start building HTML ---
    QString h;
    h += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
    h += QString("<title>%1</title>").arg(htmlEscape(opts.title));
    h += R"(<style>
      body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#f5f5f7;color:#222;margin:0;padding:24px}
      h1{font-size:20px;margin:0 0 4px}
      h2{font-size:16px;margin:28px 0 8px;border-bottom:1px solid #ddd;padding-bottom:4px}
      .muted{color:#666;font-size:12px}
      .cards{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0}
      .card{background:#fff;border:1px solid #e0e0e0;border-radius:6px;padding:10px 14px;min-width:120px}
      .card .k{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.5px}
      .card .v{font-size:18px;font-weight:600;margin-top:2px}
      table{border-collapse:collapse;background:#fff;border:1px solid #e0e0e0;border-radius:6px;overflow:hidden;width:100%;font-size:13px}
      th,td{padding:6px 10px;border-bottom:1px solid #eee;text-align:left}
      th{background:#fafafa;font-weight:600;cursor:pointer;user-select:none}
      th:hover{background:#f0f0f0}
      tr:hover td{background:#fafbff}
      .layer{display:inline-block;padding:1px 6px;border-radius:3px;color:#fff;font-size:11px}
      ul.tree,ul.tree ul{list-style:none;padding-left:16px;margin:0}
      ul.tree details>summary{cursor:pointer;padding:1px 4px;border-left:3px solid #999;margin:1px 0;font-family:Consolas,monospace;font-size:12px}
      ul.tree span.leaf{display:inline-block;padding:1px 4px;border-left:3px solid #999;margin:1px 0;font-family:Consolas,monospace;font-size:12px}
      .toolbar{margin:12px 0}
      .toolbar button{background:#fff;border:1px solid #ccc;border-radius:4px;padding:4px 10px;font-size:12px;cursor:pointer}
      .toolbar button:hover{background:#f0f0f0}
      svg{border:1px solid #333;border-radius:6px;display:block;margin-top:8px;max-width:100%}
    </style>)";
    h += R"(<script>
      function sortTable(th){
        const table=th.closest('table');
        const idx=Array.from(th.parentNode.children).indexOf(th);
        const asc=!(th.dataset.asc==='1');
        th.dataset.asc=asc?'1':'0';
        const rows=Array.from(table.tBodies[0].rows);
        const num=x=>{const m=String(x).replace(/[^\d.\-]/g,'');return m===''?NaN:parseFloat(m);};
        rows.sort((r1,r2)=>{
          const a=r1.cells[idx].innerText, b=r2.cells[idx].innerText;
          const na=num(a), nb=num(b);
          if(!isNaN(na)&&!isNaN(nb)) return asc?na-nb:nb-na;
          return asc?a.localeCompare(b):b.localeCompare(a);
        });
        rows.forEach(r=>table.tBodies[0].appendChild(r));
      }
      function expandAll(open){
        document.querySelectorAll('ul.tree details').forEach(d=>d.open=open);
      }
    </script>)";
    h += "</head><body>";

    h += QString("<h1>%1</h1>").arg(htmlEscape(opts.title));
    h += QString("<div class=\"muted\">source: %1 · generated %2 · span %3 ms</div>")
             .arg(htmlEscape(QFileInfo(path).fileName()))
             .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
             .arg(spanMs, 0, 'f', 1);

    // --- summary cards ---
    h += "<div class=\"cards\">";
    auto card = [&](const QString& k, const QString& v){
        h += QString("<div class=\"card\"><div class=\"k\">%1</div><div class=\"v\">%2</div></div>")
                 .arg(htmlEscape(k), htmlEscape(v));
    };
    card("Events",   QString::number(s.total));
    card("Enters",   QString::number(s.enters));
    card("Exits",    QString::number(s.exits));
    card("Orphans",  QString::number(s.orphanExits));
    card("Dropped",  QString::number(s.dropped));
    card("Threads",  QString::number(s.threads));
    card("Traces",   QString::number(s.traces));
    card("Blocked",  QString::number(a.totalBlockedNs / 1e6, 'f', 1) + " ms");
    h += "</div>";

    // --- layer violations ---
    h += "<h2>Layer violations</h2>";
    if (a.violations.empty()) {
        h += "<p class=\"muted\">None.</p>";
    } else {
        h += "<table><tbody>";
        h += "<tr><th>Count</th><th>Caller</th><th>Callee</th><th>Via</th></tr>";
        for (const auto& v : a.violations) {
            h += QString("<tr><td>%1x</td>"
                         "<td><span class=\"layer\" style=\"background:%2\">%3</span></td>"
                         "<td><span class=\"layer\" style=\"background:%4\">%5</span></td>"
                         "<td><code>%6</code></td></tr>")
                     .arg(v.count)
                     .arg(layerColor(v.callerLayer), htmlEscape(v.callerLayer))
                     .arg(layerColor(v.calleeLayer), htmlEscape(v.calleeLayer))
                     .arg(htmlEscape(v.via));
        }
        h += "</tbody></table>";
    }

    // --- blocking ---
    h += "<h2>GUI-thread blocking</h2>";
    if (a.blocking.empty()) {
        h += "<p class=\"muted\">None above threshold.</p>";
    } else {
        h += "<table>";
        h += tableHeader({{"Duration (ms)","right"},{"Layer","left"},
                          {"Function","left"},{"Thread","left"},{"File","left"}});
        h += "<tbody>";
        for (const auto& b : a.blocking) {
            h += QString("<tr><td style=\"text-align:right\">%1</td>"
                         "<td><span class=\"layer\" style=\"background:%2\">%3</span></td>"
                         "<td><code>%4</code></td><td>%5</td><td>%6:%7</td></tr>")
                     .arg(ms(b.dur_ns, 2))
                     .arg(layerColor(b.layer), htmlEscape(b.layer))
                     .arg(htmlEscape(b.fn))
                     .arg(htmlEscape(b.thread))
                     .arg(htmlEscape(QFileInfo(b.file).fileName()))
                     .arg(b.line);
        }
        h += "</tbody></table>";
        h += QString("<p class=\"muted\">Total blocked: %1 ms</p>")
                 .arg(a.totalBlockedNs / 1e6, 0, 'f', 2);
    }

    // --- top functions ---
    h += "<h2>Top functions by total time</h2>";
    h += "<table>";
    h += tableHeader({{"Function","left"},{"Layer","left"},{"Calls","right"},
                      {"Total (ms)","right"},{"Avg (ms)","right"},{"Max (ms)","right"}});
    h += "<tbody>";
    for (const auto& t2 : tops) {
        h += QString("<tr><td><code>%1</code></td>"
                     "<td><span class=\"layer\" style=\"background:%2\">%3</span></td>"
                     "<td style=\"text-align:right\">%4</td>"
                     "<td style=\"text-align:right\">%5</td>"
                     "<td style=\"text-align:right\">%6</td>"
                     "<td style=\"text-align:right\">%7</td></tr>")
                 .arg(htmlEscape(t2.fn))
                 .arg(layerColor(t2.layer), htmlEscape(t2.layer))
                 .arg(t2.calls)
                 .arg(ms(t2.total))
                 .arg(ms(t2.total / t2.calls))
                 .arg(ms(t2.maxv));
    }
    h += "</tbody></table>";

    // --- fan-in ---
    if (!a.fanIn.empty()) {
        h += "<h2>Fan-in hotspots</h2><table>";
        h += tableHeader({{"Function","left"},{"Distinct parents","right"},{"Calls","right"}});
        h += "<tbody>";
        for (const auto& f : a.fanIn) {
            h += QString("<tr><td><code>%1</code></td>"
                         "<td style=\"text-align:right\">%2</td>"
                         "<td style=\"text-align:right\">%3</td></tr>")
                     .arg(htmlEscape(f.fn)).arg(f.distinctParents).arg(f.calls);
        }
        h += "</tbody></table>";
    }

    // --- coupling ---
    if (!a.coupling.empty()) {
        h += "<h2>Temporal coupling</h2><table>";
        h += tableHeader({{"First","left"},{"Second","left"},
                          {"Occurrences","right"},{"Groups","right"},{"Ratio","right"}});
        h += "<tbody>";
        for (const auto& c : a.coupling) {
            h += QString("<tr><td><code>%1</code></td><td><code>%2</code></td>"
                         "<td style=\"text-align:right\">%3</td>"
                         "<td style=\"text-align:right\">%4</td>"
                         "<td style=\"text-align:right\">%5%</td></tr>")
                     .arg(htmlEscape(c.first), htmlEscape(c.second))
                     .arg(c.occurrences).arg(c.totalGroups)
                     .arg(c.ratio * 100, 0, 'f', 1);
        }
        h += "</tbody></table>";
    }

    // --- entry points ---
    if (!a.entries.empty()) {
        h += "<h2>Entry points</h2><table>";
        h += tableHeader({{"Function","left"},{"Count","right"}});
        h += "<tbody>";
        for (const auto& e : a.entries) {
            h += QString("<tr><td><code>%1</code></td>"
                         "<td style=\"text-align:right\">%2</td></tr>")
                     .arg(htmlEscape(e.fn)).arg(e.count);
        }
        h += "</tbody></table>";
    }

    // --- timeline ---
    if (opts.includeTimeline) {
        h += "<h2>Timeline</h2>";
        h += "<p class=\"muted\">One row per thread. Bar color indicates layer. Hover for details.</p>";
        h += buildTimelineSvg(t);
    }

    // --- call tree ---
    if (opts.includeTree) {
        h += "<h2>Call tree</h2>";
        h += "<div class=\"toolbar\">"
             "<button onclick=\"expandAll(true)\">Expand all</button> "
             "<button onclick=\"expandAll(false)\">Collapse all</button>"
             "</div>";
        h += "<ul class=\"tree\">";
        for (int r : t.roots) h += buildTreeHtml(t, r, 999, 0);
        h += "</ul>";
    }

    h += "</body></html>";

    // --- write atomically ---
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail("cannot open " + path + " for writing");
    {
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out << h;
    }
    if (!f.commit())
        return fail("commit failed for " + path);

    return true;
}

} // namespace rt