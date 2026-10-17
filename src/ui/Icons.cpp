#include "Icons.h"

#include "AnimatedMedia.h"

#include "Theme.h"
#include "core/Workspace.h"

#include <QImageReader>
#include <QPainterPath>

#include <QApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QSvgRenderer>

namespace
{
// Inner SVG for each icon. The root sets stroke="currentColor", no fill,
// width 1.7 and round caps/joins; dots override with fill.
const QHash<QString, const char *> &bodies()
{
    static const QHash<QString, const char *> b = {
        // ---- interface ---------------------------------------------------------------
        // ---- blocks and properties (Notion parity) ---------------------------------
        {QStringLiteral("h4"), R"svg(<path d="M4 6v12M11 6v12M4 12h7M18.5 18V8l-4 6.3h5.5"/>)svg"},
        {QStringLiteral("columns"), R"svg(<rect x="4" y="5" width="16" height="14" rx="2"/><path d="M12 5v14"/>)svg"},
        {QStringLiteral("formula"), R"svg(<path d="M15.5 5.5c-2.2-.6-3.5.4-3.9 2.6L10 17.5c-.4 2.2-1.8 3.1-4 2.5M7.5 10.5h8M14 13l5 5M19 13l-5 5"/>)svg"},
        {QStringLiteral("relation"), R"svg(<path d="M9 7H6.5A2.5 2.5 0 0 0 4 9.5v0A2.5 2.5 0 0 0 6.5 12H9M15 12h2.5A2.5 2.5 0 0 1 20 14.5v0a2.5 2.5 0 0 1-2.5 2.5H15M8.5 17h7M8.5 7h7"/><path d="M13 4.5L15.5 7 13 9.5M11 14.5L8.5 17l2.5 2.5"/>)svg"},
        {QStringLiteral("rollup"), R"svg(<path d="M5 6h14M5 12h9M5 18h5"/><path d="M17 13v6M14.5 16.5L17 19l2.5-2.5"/>)svg"},
        {QStringLiteral("paperclip"), R"svg(<path d="M18.5 11.5l-6.4 6.4a4.2 4.2 0 0 1-6-6l7.2-7.2a2.8 2.8 0 0 1 4 4l-7 7a1.4 1.4 0 0 1-2-2l6.3-6.3"/>)svg"},
        {QStringLiteral("person"), R"svg(<circle cx="12" cy="8.5" r="3.5"/><path d="M5 20c.8-3.7 3.5-5.5 7-5.5s6.2 1.8 7 5.5"/>)svg"},
        {QStringLiteral("hash"), R"svg(<path d="M9.5 4L8 20M16 4l-1.5 16M5 9h15M4 15h15"/>)svg"},
        {QStringLiteral("cursor"), R"svg(<path d="M6 4l12.5 7.2-5.6 1.4-2.6 5.4z"/><path d="M13 12.7l4.2 5"/>)svg"},
        {QStringLiteral("timeline"), R"svg(<path d="M4 5v14M4 7.5h9M8 12h10M6 16.5h7"/>)svg"},
        {QStringLiteral("feed"), R"svg(<rect x="4.5" y="4" width="15" height="7" rx="1.6"/><rect x="4.5" y="13" width="15" height="7" rx="1.6"/>)svg"},
        {QStringLiteral("form"), R"svg(<rect x="5" y="3.5" width="14" height="17" rx="2"/><path d="M8.5 8h7M8.5 12h7M8.5 16h4"/>)svg"},
        {QStringLiteral("sigma"), R"svg(<path d="M17.5 6V4.5h-11l6 7.5-6 7.5h11V18"/>)svg"},
        {QStringLiteral("toc"), R"svg(<path d="M5 6h14M8 10h11M8 14h11M11 18h8"/><circle cx="5" cy="10" r=".9" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("breadcrumb"), R"svg(<rect x="3.5" y="8.5" width="5" height="7" rx="1.2"/><path d="M11 9.5l2.5 2.5-2.5 2.5M16 9.5l2.5 2.5-2.5 2.5"/>)svg"},
        {QStringLiteral("button"), R"svg(<rect x="3.5" y="7" width="17" height="10" rx="3"/><path d="M8.5 12h7"/>)svg"},
        {QStringLiteral("embed"), R"svg(<path d="M8.5 8l-4 4 4 4M15.5 8l4 4-4 4M13.5 5.5l-3 13"/>)svg"},
        {QStringLiteral("video"), R"svg(<rect x="3.5" y="6" width="12.5" height="12" rx="2"/><path d="M16 10.5l4.5-2.5v8L16 13.5"/>)svg"},
        {QStringLiteral("audio"), R"svg(<path d="M4.5 9.5h3.2L12 5.5v13l-4.3-4H4.5z"/><path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11"/>)svg"},
        {QStringLiteral("file"), R"svg(<path d="M7 3.5h7l4.5 4.5v11a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5z"/><path d="M13.5 3.5V8.5h5"/>)svg"},
        {QStringLiteral("pdf"), R"svg(<path d="M7 3.5h7l4.5 4.5v11a1.5 1.5 0 0 1-1.5 1.5H7A1.5 1.5 0 0 1 5.5 19V5A1.5 1.5 0 0 1 7 3.5z"/><path d="M8.5 16.5v-4h1.4a1.3 1.3 0 0 1 0 2.6H8.5M13 16.5v-4h1a2 2 0 0 1 0 4zM17 12.5h-1.5v4M15.5 14.5h1.2"/>)svg"},
        {QStringLiteral("synced"), R"svg(<path d="M19 9a7.5 7.5 0 0 0-13.4-2M5 15a7.5 7.5 0 0 0 13.4 2"/><path d="M5 4v4h4M19 20v-4h-4"/>)svg"},
        {QStringLiteral("comment"), R"svg(<path d="M5 5.5h14a1.5 1.5 0 0 1 1.5 1.5v8.5A1.5 1.5 0 0 1 19 17h-8l-4.5 3.5V17H5a1.5 1.5 0 0 1-1.5-1.5V7A1.5 1.5 0 0 1 5 5.5z"/>)svg"},
        {QStringLiteral("history"), R"svg(<path d="M4.5 12a7.5 7.5 0 1 0 2.2-5.3L4.5 9"/><path d="M4.5 4.5V9H9M12 8v4.5l3 2"/>)svg"},
        {QStringLiteral("bell"), R"svg(<path d="M6.5 16.5V11a5.5 5.5 0 0 1 11 0v5.5l1.5 1.5H5z"/><path d="M10 20.5a2.2 2.2 0 0 0 4 0"/>)svg"},
        {QStringLiteral("mermaid"), R"svg(<rect x="4" y="4" width="6" height="5" rx="1.2"/><rect x="14" y="15" width="6" height="5" rx="1.2"/><rect x="4" y="15" width="6" height="5" rx="1.2"/><path d="M7 9v6M10 6.5h4.5a2.5 2.5 0 0 1 2.5 2.5v6"/>)svg"},
        {QStringLiteral("tab"), R"svg(<path d="M3.5 19V7a1.5 1.5 0 0 1 1.5-1.5h5L12 8h7a1.5 1.5 0 0 1 1.5 1.5V19"/><path d="M3 19h18"/>)svg"},
        {QStringLiteral("anchor"), R"svg(<circle cx="12" cy="5.5" r="2"/><path d="M12 7.5V20M5 13a7 7 0 0 0 14 0M8.5 10.5h7"/>)svg"},
        {QStringLiteral("spell"), R"svg(<path d="M4 15l3.5-9 3.5 9M5.3 12h4.4M13 10.5l2.5 2.5 4.5-5"/><path d="M4 19h16" stroke-dasharray="1.5 2"/>)svg"},
        {QStringLiteral("fullscreen"), R"svg(<path d="M4.5 9V4.5H9M15 4.5h4.5V9M19.5 15v4.5H15M9 19.5H4.5V15"/>)svg"},
        {QStringLiteral("align-left"), R"svg(<path d="M4 6h16M4 10h10M4 14h16M4 18h10"/>)svg"},
        {QStringLiteral("align-center"), R"svg(<path d="M4 6h16M7 10h10M4 14h16M7 18h10"/>)svg"},
        {QStringLiteral("align-right"), R"svg(<path d="M4 6h16M10 10h10M4 14h16M10 18h10"/>)svg"},
        {QStringLiteral("caption"), R"svg(<rect x="4" y="4" width="16" height="11" rx="2"/><path d="M6 19h12"/>)svg"},
        {QStringLiteral("wrap"), R"svg(<path d="M4 6h16M4 12h13a3 3 0 0 1 0 6h-4M15 16l-2 2 2 2M4 18h5"/>)svg"},
        {QStringLiteral("key-command"), R"svg(<path d="M9 9v6M15 9v6M9 9h6M9 15h6M9 9H7.5A1.5 1.5 0 1 1 9 7.5zM15 9h1.5A1.5 1.5 0 1 0 15 7.5zM9 15H7.5A1.5 1.5 0 1 0 9 16.5zM15 15h1.5a1.5 1.5 0 1 1-1.5 1.5z"/>)svg"},
        {QStringLiteral("group"), R"svg(<rect x="4" y="4" width="16" height="6" rx="1.5"/><rect x="4" y="14" width="16" height="6" rx="1.5"/><path d="M7 7h4M7 17h4"/>)svg"},
        {QStringLiteral("calc"), R"svg(<rect x="5" y="3.5" width="14" height="17" rx="2"/><path d="M8 7.5h8M8.5 12h1M11.5 12h1M14.5 12h1M8.5 15.5h1M11.5 15.5h1M14.5 15.5h1"/>)svg"},
        {QStringLiteral("peek-side"), R"svg(<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><path d="M13 4.5v15"/>)svg"},
        {QStringLiteral("peek-center"), R"svg(<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><rect x="7" y="7.5" width="10" height="9" rx="1"/>)svg"},
        {QStringLiteral("full-page"), R"svg(<rect x="3.5" y="4.5" width="17" height="15" rx="2"/><path d="M7 8.5h10M7 12h10M7 15.5h6"/>)svg"},
        {QStringLiteral("freeze"), R"svg(<path d="M12 3.5v17M4.6 7.75l14.8 8.5M4.6 16.25l14.8-8.5M9.5 4.5L12 7l2.5-2.5M9.5 19.5L12 17l2.5 2.5"/>)svg"},
        {QStringLiteral("subitems"), R"svg(<path d="M5 5h9M9 12h9M9 19h9M6 7.5v10.5a1 1 0 0 0 1 1h1M6 12h2"/>)svg"},
        {QStringLiteral("dependency"), R"svg(<rect x="3.5" y="5" width="7" height="4.5" rx="1"/><rect x="13.5" y="14.5" width="7" height="4.5" rx="1"/><path d="M10.5 7.2h2a2.5 2.5 0 0 1 2.5 2.5v2.5M13 10.5l2 2 2-2"/>)svg"},
        {QStringLiteral("number-bar"), R"svg(<rect x="3.5" y="10" width="17" height="4" rx="2"/><path d="M5.5 12h7"/>)svg"},
        {QStringLiteral("number-ring"), R"svg(<circle cx="12" cy="12" r="7"/><path d="M12 5a7 7 0 0 1 6.6 9.3"/>)svg"},
        {QStringLiteral("window-new"), R"svg(<rect x="3.5" y="5" width="13" height="11" rx="1.5"/><path d="M7.5 19.5h11a1.5 1.5 0 0 0 1.5-1.5V9"/>)svg"},
        {QStringLiteral("tab-new"), R"svg(<path d="M3.5 18V8a1.5 1.5 0 0 1 1.5-1.5h5L12 9h7a1.5 1.5 0 0 1 1.5 1.5V18a1.5 1.5 0 0 1-1.5 1.5H5A1.5 1.5 0 0 1 3.5 18z"/><path d="M12 12v5M9.5 14.5h5"/>)svg"},
        {QStringLiteral("search"), R"svg(<circle cx="11" cy="11" r="6.5"/><path d="M16 16l4.5 4.5"/>)svg"},
        {QStringLiteral("home"), R"svg(<path d="M4 10.5L12 4l8 6.5V19a1 1 0 0 1-1 1h-4.5v-5.5h-5V20H5a1 1 0 0 1-1-1z"/>)svg"},
        {QStringLiteral("plus"), R"svg(<path d="M12 5v14M5 12h14"/>)svg"},
        {QStringLiteral("minus"), R"svg(<path d="M5 12h14"/>)svg"},
        {QStringLiteral("chevron-right"), R"svg(<path d="M9.5 6l6 6-6 6"/>)svg"},
        {QStringLiteral("chevron-down"), R"svg(<path d="M6 9.5l6 6 6-6"/>)svg"},
        {QStringLiteral("chevron-left"), R"svg(<path d="M14.5 6l-6 6 6 6"/>)svg"},
        {QStringLiteral("chevron-up"), R"svg(<path d="M6 14.5l6-6 6 6"/>)svg"},
        {QStringLiteral("chevrons-left"), R"svg(<path d="M12 6l-6 6 6 6M18.5 6l-6 6 6 6"/>)svg"},
        {QStringLiteral("more"), R"svg(<circle cx="5.5" cy="12" r="1.4" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.4" fill="currentColor" stroke="none"/><circle cx="18.5" cy="12" r="1.4" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("star"), R"svg(<path d="M12 3.8l2.5 5.2 5.7.8-4.1 4 1 5.7L12 16.8l-5.1 2.7 1-5.7-4.1-4 5.7-.8z"/>)svg"},
        {QStringLiteral("star-filled"), R"svg(<path d="M12 3.8l2.5 5.2 5.7.8-4.1 4 1 5.7L12 16.8l-5.1 2.7 1-5.7-4.1-4 5.7-.8z" fill="currentColor"/>)svg"},
        {QStringLiteral("trash"), R"svg(<path d="M4.5 7h15M9.5 7V4.8h5V7M6.5 7l.9 12.2a1 1 0 0 0 1 .8h7.2a1 1 0 0 0 1-.8L17.5 7M10.2 10.8v5.6M13.8 10.8v5.6"/>)svg"},
        {QStringLiteral("settings"), R"svg(<path d="M4 7h9M17 7h3M4 17h3M11 17h9"/><circle cx="15" cy="7" r="2.1"/><circle cx="9" cy="17" r="2.1"/>)svg"},
        {QStringLiteral("download"), R"svg(<path d="M12 4v11M7.5 10.5L12 15l4.5-4.5M5 19.5h14"/>)svg"},
        {QStringLiteral("upload"), R"svg(<path d="M12 15V4M7.5 8.5L12 4l4.5 4.5M5 19.5h14"/>)svg"},
        {QStringLiteral("template"), R"svg(<rect x="4" y="4" width="16" height="16" rx="2.5"/><path d="M4 9.5h16M9.5 9.5V20"/>)svg"},
        {QStringLiteral("sidebar"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><path d="M9.5 5v14"/>)svg"},
        {QStringLiteral("arrow-left"), R"svg(<path d="M19 12H5M11 6l-6 6 6 6"/>)svg"},
        {QStringLiteral("arrow-right"), R"svg(<path d="M5 12h14M13 6l6 6-6 6"/>)svg"},
        {QStringLiteral("arrow-up"), R"svg(<path d="M12 19V5M6 11l6-6 6 6"/>)svg"},
        {QStringLiteral("arrow-down"), R"svg(<path d="M12 5v14M6 13l6 6 6-6"/>)svg"},
        {QStringLiteral("close"), R"svg(<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>)svg"},
        {QStringLiteral("win-min"), R"svg(<path d="M7 12h10"/>)svg"},
        {QStringLiteral("win-max"), R"svg(<rect x="7" y="7" width="10" height="10" rx="1.8"/>)svg"},
        {QStringLiteral("win-restore"), R"svg(<rect x="6.5" y="9" width="8.5" height="8.5" rx="1.6"/><path d="M9.5 6.5h6.2a1.8 1.8 0 0 1 1.8 1.8v6.2"/>)svg"},
        {QStringLiteral("page"), R"svg(<path d="M7 3.5h7l4.5 4.5v11.5a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-15a1 1 0 0 1 1-1z"/><path d="M13.5 3.5v5h5M9 13h6M9 16.5h4"/>)svg"},
        {QStringLiteral("page-empty"), R"svg(<path d="M7 3.5h7l4.5 4.5v11.5a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1v-15a1 1 0 0 1 1-1z"/><path d="M13.5 3.5v5h5"/>)svg"},
        {QStringLiteral("database"), R"svg(<ellipse cx="12" cy="6.5" rx="7" ry="2.8"/><path d="M5 6.5v11c0 1.5 3.1 2.8 7 2.8s7-1.3 7-2.8v-11M5 12c0 1.5 3.1 2.8 7 2.8s7-1.3 7-2.8"/>)svg"},
        {QStringLiteral("table"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="2"/><path d="M3.5 10h17M3.5 14.5h17M9.5 10v9"/>)svg"},
        {QStringLiteral("board"), R"svg(<rect x="3.5" y="4.5" width="4.8" height="15" rx="1.4"/><rect x="9.6" y="4.5" width="4.8" height="10" rx="1.4"/><rect x="15.7" y="4.5" width="4.8" height="12.5" rx="1.4"/>)svg"},
        {QStringLiteral("list"), R"svg(<path d="M9 7h11M9 12h11M9 17h11"/><circle cx="5" cy="7" r="1.1" fill="currentColor" stroke="none"/><circle cx="5" cy="12" r="1.1" fill="currentColor" stroke="none"/><circle cx="5" cy="17" r="1.1" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("gallery"), R"svg(<rect x="4" y="4" width="7" height="7" rx="1.6"/><rect x="13" y="4" width="7" height="7" rx="1.6"/><rect x="4" y="13" width="7" height="7" rx="1.6"/><rect x="13" y="13" width="7" height="7" rx="1.6"/>)svg"},
        {QStringLiteral("calendar"), R"svg(<rect x="4" y="5.5" width="16" height="14.5" rx="2.5"/><path d="M4 10h16M8.5 3.5v4M15.5 3.5v4"/>)svg"},
        {QStringLiteral("filter"), R"svg(<path d="M4 6.5h16M7 12h10M10 17.5h4"/>)svg"},
        {QStringLiteral("sort"), R"svg(<path d="M8 5v14M4.5 15.5L8 19l3.5-3.5M16 19V5M12.5 8.5L16 5l3.5 3.5"/>)svg"},
        {QStringLiteral("grip"), R"svg(<circle cx="9" cy="6.5" r="1.45" fill="currentColor" stroke="none"/><circle cx="15" cy="6.5" r="1.45" fill="currentColor" stroke="none"/><circle cx="9" cy="12" r="1.45" fill="currentColor" stroke="none"/><circle cx="15" cy="12" r="1.45" fill="currentColor" stroke="none"/><circle cx="9" cy="17.5" r="1.45" fill="currentColor" stroke="none"/><circle cx="15" cy="17.5" r="1.45" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("copy"), R"svg(<rect x="8.5" y="8.5" width="11" height="11" rx="2"/><path d="M15.5 8.5v-2a2 2 0 0 0-2-2h-7a2 2 0 0 0-2 2v7a2 2 0 0 0 2 2h2"/>)svg"},
        {QStringLiteral("link"), R"svg(<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>)svg"},
        {QStringLiteral("external"), R"svg(<path d="M8 16L17 7M9.5 7H17v7.5"/>)svg"},
        {QStringLiteral("text"), R"svg(<path d="M5 7V5h14v2M12 5v14M9.5 19h5"/>)svg"},
        {QStringLiteral("title"), R"svg(<path d="M3.5 18.5L8 6h1.2l4.5 12.5M5.1 14.3h6.9"/><path d="M15.3 11.3a2.6 2.6 0 0 1 5 .9v6.3M20.3 14.8h-2.6a2 2 0 0 0 0 4c1.5 0 2.6-1 2.6-2.4"/>)svg"},
        {QStringLiteral("number"), R"svg(<path d="M9.8 4.5L8.2 19.5M15.8 4.5l-1.6 15M5 9.2h14.5M4.5 14.8H19"/>)svg"},
        {QStringLiteral("select"), R"svg(<circle cx="12" cy="12" r="8"/><path d="M8.8 10.8L12 14l3.2-3.2"/>)svg"},
        {QStringLiteral("multi"), R"svg(<path d="M3.5 11.6V5.5a2 2 0 0 1 2-2h6.1l8.9 8.9-8.1 8.1z"/><circle cx="8" cy="8" r="1.4"/>)svg"},
        {QStringLiteral("status"), R"svg(<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 1 0 16z" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("checkbox"), R"svg(<rect x="4.5" y="4.5" width="15" height="15" rx="3.5"/><path d="M8.5 12.2l2.5 2.5 4.6-5"/>)svg"},
        {QStringLiteral("at"), R"svg(<circle cx="12" cy="12" r="3.5"/><path d="M15.5 12v1.5a2.5 2.5 0 0 0 5 0V12a8.5 8.5 0 1 0-3.4 6.8"/>)svg"},
        {QStringLiteral("phone"), R"svg(<path d="M5.2 4.5h3.3l1.7 4.2-2.2 1.4a10 10 0 0 0 5.9 5.9l1.4-2.2 4.2 1.7v3.3a1.5 1.5 0 0 1-1.6 1.5A15.5 15.5 0 0 1 3.7 6.1a1.5 1.5 0 0 1 1.5-1.6z"/>)svg"},
        {QStringLiteral("clock"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>)svg"},
        {QStringLiteral("eye"), R"svg(<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>)svg"},
        {QStringLiteral("eye-off"), R"svg(<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/><path d="M4 4l16 16"/>)svg"},
        {QStringLiteral("image"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><circle cx="9" cy="10" r="1.6"/><path d="M20.5 15.5L16 11l-8.5 8"/>)svg"},
        {QStringLiteral("smile"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M8.5 14.3a4.5 4.5 0 0 0 7 0"/><circle cx="9.2" cy="10" r="1" fill="currentColor" stroke="none"/><circle cx="14.8" cy="10" r="1" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("shuffle"), R"svg(<path d="M4 7h3.5c4.5 0 4.5 10 9 10H20M17.5 14.5L20 17l-2.5 2.5M4 17h3.5c1.5 0 2.5-1.1 3.3-2.6M13.2 9.6C14 8.1 15 7 16.5 7H20M17.5 4.5L20 7l-2.5 2.5"/>)svg"},
        {QStringLiteral("restore"), R"svg(<path d="M4.5 12a7.5 7.5 0 1 0 2.2-5.3L4.5 9"/><path d="M4.5 4.5V9H9"/>)svg"},
        {QStringLiteral("lock"), R"svg(<rect x="5" y="10.5" width="14" height="10" rx="2.5"/><path d="M8 10.5V8a4 4 0 0 1 8 0v2.5"/>)svg"},
        {QStringLiteral("unlock"), R"svg(<rect x="5" y="10.5" width="14" height="10" rx="2.5"/><path d="M8 10.5V8a4 4 0 0 1 7.7-1.5"/>)svg"},
        {QStringLiteral("code"), R"svg(<path d="M8.5 7.5L4 12l4.5 4.5M15.5 7.5L20 12l-4.5 4.5M13.5 5l-3 14"/>)svg"},
        {QStringLiteral("quote"), R"svg(<path d="M5.5 6.5v11M10 8.5h9M10 12h9M10 15.5h6"/>)svg"},
        {QStringLiteral("h1"), R"svg(<path d="M4.5 6v12M11.5 6v12M4.5 12h7M16.3 9.6L19 7.5V18"/>)svg"},
        {QStringLiteral("h2"), R"svg(<path d="M4 6v12M11 6v12M4 12h7M15.2 9.2a2.4 2.4 0 0 1 4.6.9c0 1.9-4.6 4.6-4.6 7.9h4.8"/>)svg"},
        {QStringLiteral("h3"), R"svg(<path d="M4 6v12M11 6v12M4 12h7M15.4 8.4a2.3 2.3 0 0 1 4.3 1.1c0 1.3-1 2.2-2.4 2.2 1.6 0 2.7 1 2.7 2.6a2.5 2.5 0 0 1-4.8 1"/>)svg"},
        {QStringLiteral("bullet-list"), R"svg(<circle cx="5.5" cy="7" r="1.35" fill="currentColor" stroke="none"/><circle cx="5.5" cy="12" r="1.35" fill="currentColor" stroke="none"/><circle cx="5.5" cy="17" r="1.35" fill="currentColor" stroke="none"/><path d="M9.5 7H20M9.5 12H20M9.5 17H20"/>)svg"},
        {QStringLiteral("numbered-list"), R"svg(<path d="M10 7h10M10 12h10M10 17h10M4.6 5.4L6 4.7v4.6M4 14.4a1.2 1.2 0 0 1 2.3.4c0 .9-2.3 1.8-2.3 3.1h2.5"/>)svg"},
        {QStringLiteral("toggle"), R"svg(<path d="M5 6.3l4 2.7-4 2.7z" fill="currentColor" stroke-width="1.2"/><path d="M12 9h8M8 15h12M8 18.5h8"/>)svg"},
        {QStringLiteral("callout"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="3"/><circle cx="8" cy="10" r="1.3" fill="currentColor" stroke="none"/><path d="M11 10h6M8 14.5h9"/>)svg"},
        {QStringLiteral("divider"), R"svg(<path d="M4 12h16"/><path d="M8 7.5h8M8 16.5h8" opacity=".4"/>)svg"},
        {QStringLiteral("pencil"), R"svg(<path d="M15.5 4.5l4 4L9 19H5v-4z"/><path d="M13.5 6.5l4 4"/>)svg"},
        {QStringLiteral("move"), R"svg(<path d="M4 7.5a2 2 0 0 1 2-2h3.5l2 2H18a2 2 0 0 1 2 2V17a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2z"/><path d="M10 13.5h5.5M13.5 11l2.5 2.5-2.5 2.5"/>)svg"},
        {QStringLiteral("check"), R"svg(<path d="M5.5 12.5l4.2 4.2 8.8-9.2"/>)svg"},
        {QStringLiteral("info"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M12 11v5.5"/><circle cx="12" cy="7.8" r="1" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("alert"), R"svg(<path d="M12 4.5l8.5 14.5h-17z"/><path d="M12 10v4"/><circle cx="12" cy="16.6" r=".9" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("font"), R"svg(<path d="M5.5 19L11.3 5h1.4L18.5 19M8 13.5h8"/>)svg"},
        {QStringLiteral("width"), R"svg(<path d="M4 12h16M7.5 8.5L4 12l3.5 3.5M16.5 8.5L20 12l-3.5 3.5"/><path d="M4 5v2M20 5v2M4 17v2M20 17v2" opacity=".5"/>)svg"},
        {QStringLiteral("textsize"), R"svg(<path d="M3.5 7V5.5h10V7M8.5 5.5V18M13.5 12v-1.5h7V12M17 10.5V18"/>)svg"},
        {QStringLiteral("pin"), R"svg(<path d="M9 4.5h6l-1 5 3 3V14H7v-1.5l3-3zM12 14v6"/>)svg"},
        {QStringLiteral("dot"), R"svg(<circle cx="12" cy="12" r="3" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("enter"), R"svg(<path d="M19 6v5a3 3 0 0 1-3 3H6M9.5 10.5L6 14l3.5 3.5"/>)svg"},
        {QStringLiteral("command"), R"svg(<path d="M9 9h6v6H9zM9 9V7a2 2 0 1 0-2 2zM15 9V7a2 2 0 1 1 2 2zM9 15v2a2 2 0 1 1-2-2zM15 15v2a2 2 0 1 0 2-2z"/>)svg"},
        {QStringLiteral("window"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><path d="M3.5 9h17"/>)svg"},
        {QStringLiteral("refresh"), R"svg(<path d="M19.5 12a7.5 7.5 0 1 1-2.2-5.3L19.5 9"/><path d="M19.5 4.5V9H15"/>)svg"},

        // ---- space -------------------------------------------------------------------
        {QStringLiteral("planet"), R"svg(<circle cx="12" cy="12" r="5.5"/><path d="M6.7 10.2C3.4 12.2 1.9 14.1 2.5 15.2c.9 1.6 6 .3 11.3-2.8S22.4 5.4 21.5 3.8c-.6-1.1-2.9-.8-6 .5"/>)svg"},
        {QStringLiteral("moon"), R"svg(<path d="M19.5 14.5A8 8 0 0 1 9.5 4.5a8 8 0 1 0 10 10z"/>)svg"},
        {QStringLiteral("sun"), R"svg(<circle cx="12" cy="12" r="4"/><path d="M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6L7 7M17 17l1.4 1.4M5.6 18.4L7 17M17 7l1.4-1.4"/>)svg"},
        {QStringLiteral("sparkle"), R"svg(<path d="M12 3.5c.6 4.2 2.3 5.9 6.5 6.5-4.2.6-5.9 2.3-6.5 6.5-.6-4.2-2.3-5.9-6.5-6.5 4.2-.6 5.9-2.3 6.5-6.5z"/><path d="M18.5 15.5c.3 1.6.9 2.2 2.5 2.5-1.6.3-2.2.9-2.5 2.5-.3-1.6-.9-2.2-2.5-2.5 1.6-.3 2.2-.9 2.5-2.5z"/>)svg"},
        {QStringLiteral("rocket"), R"svg(<path d="M14.5 4.5c2.6-.7 4.4-.5 5 0 .5.6.7 2.4 0 5l-6.8 6.8-4.5-4.5z"/><path d="M8.2 11.8L5 11.5 7.5 9H11M12.2 15.8l.3 3.2L15 16.5V13"/><path d="M6.5 15.5c-1.5.5-2.2 2.2-2.5 4 1.8-.3 3.5-1 4-2.5"/><circle cx="15.5" cy="8.5" r="1.4"/>)svg"},
        {QStringLiteral("comet"), R"svg(<circle cx="16" cy="8" r="3.5"/><path d="M13.5 10.5L4 20M12 7l-6.5 6.5M17 12.5l-5.5 5.5"/>)svg"},
        {QStringLiteral("telescope"), R"svg(<path d="M3.8 12.6l12-6.5 1.9 3.5-12 6.5zM15.8 6.1l2.6-1.4 1.9 3.5-2.6 1.4M10.2 13.3L7.5 20.5M11.6 12.6l3 7.9"/>)svg"},
        {QStringLiteral("galaxy"), R"svg(<path d="M12 12c0-1.2 1.6-1.6 2.4-.8 1.4 1.4.3 4-1.9 4.3-3 .4-5.3-2.5-4.8-5.5.7-4 5.4-6 9-4.3M12 12c0 1.2-1.6 1.6-2.4.8-1.4-1.4-.3-4 1.9-4.3 3-.4 5.3 2.5 4.8 5.5-.7 4-5.4 6-9 4.3"/>)svg"},
        {QStringLiteral("orbit"), R"svg(<circle cx="12" cy="12" r="2.4"/><ellipse cx="12" cy="12" rx="9" ry="4" transform="rotate(-25 12 12)"/><circle cx="19.3" cy="8.4" r="1.4" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("constellation"), R"svg(<path d="M4.5 17.5l5-6 4.5 3 5.5-8"/><circle cx="4.5" cy="17.5" r="1.5" fill="currentColor" stroke="none"/><circle cx="9.5" cy="11.5" r="1.5" fill="currentColor" stroke="none"/><circle cx="14" cy="14.5" r="1.5" fill="currentColor" stroke="none"/><circle cx="19.5" cy="6.5" r="1.5" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("eclipse"), R"svg(<circle cx="12" cy="12" r="6" fill="currentColor" stroke="none" opacity=".25"/><circle cx="12" cy="12" r="6"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/>)svg"},
        {QStringLiteral("satellite"), R"svg(<path d="M9.5 12.5l2-2 4 4-2 2zM7.5 10.5l-3-3 2-2 3 3M15.5 18.5l3 3 2-2-3-3M13 9l2-2M16 6l2-2"/><path d="M5 19c1.5 0 3-1.5 3-3"/>)svg"},
        {QStringLiteral("ufo"), R"svg(<ellipse cx="12" cy="13" rx="9" ry="3"/><path d="M7.5 11.3C7.8 8.3 9.7 6.5 12 6.5s4.2 1.8 4.5 4.8M8 18l-1.5 2.5M16 18l1.5 2.5M12 16.5v3"/>)svg"},
        {QStringLiteral("astronaut"), R"svg(<circle cx="12" cy="9" r="5.5"/><rect x="8.8" y="7" width="6.4" height="4" rx="2"/><path d="M6 20.5v-2a4 4 0 0 1 4-4h4a4 4 0 0 1 4 4v2"/>)svg"},

        // ---- general -----------------------------------------------------------------
        {QStringLiteral("book"), R"svg(<path d="M5.5 18.5v-13A1.5 1.5 0 0 1 7 4h11v13H7a1.5 1.5 0 0 0-1.5 1.5zm0 0A1.5 1.5 0 0 0 7 20h11"/>)svg"},
        {QStringLiteral("notebook"), R"svg(<rect x="5.5" y="3.5" width="13" height="17" rx="2"/><path d="M9 3.5v17M3.5 7.5h3M3.5 12h3M3.5 16.5h3"/>)svg"},
        {QStringLiteral("bulb"), R"svg(<path d="M9 17.5h6M10 20.5h4M12 3.5a6 6 0 0 0-3.6 10.8c.6.5 1 1.3 1 2.2v1h5.2v-1c0-.9.4-1.7 1-2.2A6 6 0 0 0 12 3.5z"/>)svg"},
        {QStringLiteral("target"), R"svg(<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1.5" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("flag"), R"svg(<path d="M5.5 20.5v-16M5.5 5c4-2 7 2 11.5 0v8.5c-4.5 2-7.5-2-11.5 0"/>)svg"},
        {QStringLiteral("heart"), R"svg(<path d="M12 19.5s-7.5-4.5-7.5-10a4.2 4.2 0 0 1 7.5-2.6 4.2 4.2 0 0 1 7.5 2.6c0 5.5-7.5 10-7.5 10z"/>)svg"},
        {QStringLiteral("music"), R"svg(<path d="M9 18V5.5l11-2V16"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="17.5" cy="16" r="2.5"/>)svg"},
        {QStringLiteral("camera"), R"svg(<path d="M4 8.5A1.5 1.5 0 0 1 5.5 7h2.8l1.5-2.2h4.4L15.7 7h2.8A1.5 1.5 0 0 1 20 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 17.5z"/><circle cx="12" cy="12.8" r="3.2"/>)svg"},
        {QStringLiteral("coffee"), R"svg(<path d="M5 9.5h11v5a5 5 0 0 1-5 5h-1a5 5 0 0 1-5-5zM16 11h1.5a2.5 2.5 0 0 1 0 5H16M8.5 3.5c-.8 1 .8 2-.1 3M12 3.5c-.8 1 .8 2-.1 3"/>)svg"},
        {QStringLiteral("leaf"), R"svg(<path d="M5 19c0-8 5-13.5 14.5-14 .2 9.5-5.5 14.5-13 14zM5 19l7-7"/>)svg"},
        {QStringLiteral("mountain"), R"svg(<path d="M3 19.5L9.5 8l3.5 6 2-3.2 6 8.7z"/>)svg"},
        {QStringLiteral("compass"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>)svg"},
        {QStringLiteral("globe"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M3.5 12h17M12 3.5c2.5 2.5 3.5 5.5 3.5 8.5s-1 6-3.5 8.5M12 3.5C9.5 6 8.5 9 8.5 12s1 6 3.5 8.5"/>)svg"},
        {QStringLiteral("briefcase"), R"svg(<rect x="3.5" y="7" width="17" height="12.5" rx="2"/><path d="M9 7V5.5A1.5 1.5 0 0 1 10.5 4h3A1.5 1.5 0 0 1 15 5.5V7M3.5 12.5h17"/>)svg"},
        {QStringLiteral("graduation"), R"svg(<path d="M2.5 9.5L12 5l9.5 4.5L12 14z"/><path d="M6.5 11.5V16c1.5 1.5 3.5 2.2 5.5 2.2s4-.7 5.5-2.2v-4.5M21.5 9.5v5"/>)svg"},
        {QStringLiteral("terminal"), R"svg(<rect x="3.5" y="5" width="17" height="14" rx="2.5"/><path d="M7.5 9.5l3 2.5-3 2.5M12.5 15h4"/>)svg"},
        {QStringLiteral("chart"), R"svg(<path d="M4.5 19.5h15M7 16v-4M11 16V8M15 16v-6M19 16V5"/>)svg"},
        {QStringLiteral("check-circle"), R"svg(<circle cx="12" cy="12" r="8.5"/><path d="M8.3 12.3l2.6 2.6 5-5.4"/>)svg"},
        {QStringLiteral("bookmark"), R"svg(<path d="M6.5 4.5h11v15.5L12 16l-5.5 4z"/>)svg"},
        {QStringLiteral("inbox"), R"svg(<path d="M4 13.5l2.2-7.3A1.5 1.5 0 0 1 7.6 5h8.8a1.5 1.5 0 0 1 1.4 1.2l2.2 7.3V18a1.5 1.5 0 0 1-1.5 1.5h-13A1.5 1.5 0 0 1 4 18z"/><path d="M4 13.5h4.5l1.2 2h4.6l1.2-2H20"/>)svg"},
        {QStringLiteral("folder"), R"svg(<path d="M3.5 7.5a2 2 0 0 1 2-2h4l2 2.2h7a2 2 0 0 1 2 2V17a2 2 0 0 1-2 2h-13a2 2 0 0 1-2-2z"/>)svg"},
        {QStringLiteral("tag"), R"svg(<path d="M3.5 11.6V5.5a2 2 0 0 1 2-2h6.1l8.9 8.9-8.1 8.1z"/><circle cx="8" cy="8" r="1.4"/>)svg"},
        {QStringLiteral("gift"), R"svg(<rect x="4" y="9" width="16" height="4" rx="1"/><path d="M5.5 13v6.5h13V13M12 9v10.5M12 9c-1.5-3.5-5.5-4-5.5-1.5S10 9 12 9zm0 0c1.5-3.5 5.5-4 5.5-1.5S14 9 12 9z"/>)svg"},
        {QStringLiteral("people"), R"svg(<circle cx="9" cy="9" r="3.2"/><path d="M3.5 19a5.5 5.5 0 0 1 11 0M15.5 6a3 3 0 0 1 0 6M17.5 14.2a5.3 5.3 0 0 1 3 4.8"/>)svg"},
        {QStringLiteral("chat"), R"svg(<path d="M4.5 6.5a2 2 0 0 1 2-2h11a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H10l-4 3.5v-3.5A1.5 1.5 0 0 1 4.5 15z"/>)svg"},
        {QStringLiteral("mail"), R"svg(<rect x="3.5" y="5.5" width="17" height="13" rx="2"/><path d="M4 7l8 6 8-6"/>)svg"},
        {QStringLiteral("key"), R"svg(<circle cx="8" cy="15" r="3.8"/><path d="M10.8 12.3L19 4.2M16.5 6.7l2.3 2.3M14.3 8.9l1.8 1.8"/>)svg"},
        {QStringLiteral("shield"), R"svg(<path d="M12 3.5l7 2.8v5.2c0 4.5-3 7.6-7 9-4-1.4-7-4.5-7-9V6.3z"/>)svg"},
        {QStringLiteral("fire"), R"svg(<path d="M12 20.5a6 6 0 0 0 6-6c0-4.5-4-6.5-4.5-10.5C10.5 6 8.8 8.3 8.8 10.5 7.6 10 7 9 7 8c-1.5 1.5-1 3.5-1 6.5a6 6 0 0 0 6 6z"/>)svg"},
        {QStringLiteral("bolt"), R"svg(<path d="M13 3.5L5.5 13.5H12l-1 7 7.5-10H12z"/>)svg"},
        {QStringLiteral("diamond"), R"svg(<path d="M6.5 4.5h11l3.5 5-9 10-9-10z"/><path d="M3 9.5h18M9.5 4.5L8 9.5l4 10 4-10-1.5-5"/>)svg"},
        {QStringLiteral("palette"), R"svg(<path d="M12 3.5a8.5 8.5 0 0 0 0 17c1.2 0 1.8-.8 1.8-1.7 0-.6-.3-1-.6-1.4-.3-.4-.5-.8-.5-1.3 0-1 .8-1.6 1.8-1.6h2.1a3.9 3.9 0 0 0 3.9-3.9C20.5 7 16.7 3.5 12 3.5z"/><circle cx="7.8" cy="11.5" r="1.1" fill="currentColor" stroke="none"/><circle cx="10.5" cy="7.6" r="1.1" fill="currentColor" stroke="none"/><circle cx="15" cy="7.8" r="1.1" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("film"), R"svg(<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M8 4v16M16 4v16M4 8h4M4 12h4M4 16h4M16 8h4M16 12h4M16 16h4"/>)svg"},
        {QStringLiteral("gamepad"), R"svg(<path d="M7 8.5h10a4.5 4.5 0 0 1 4.3 5.8l-.8 2.7a2.3 2.3 0 0 1-4 .7L15 16H9l-1.5 1.7a2.3 2.3 0 0 1-4-.7l-.8-2.7A4.5 4.5 0 0 1 7 8.5z"/><path d="M8 11v3M6.5 12.5h3"/><circle cx="15.5" cy="11.5" r=".9" fill="currentColor" stroke="none"/><circle cx="17" cy="13.5" r=".9" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("wallet"), R"svg(<path d="M4 7.5a2 2 0 0 1 2-2h11.5v3M4 7.5v10a2 2 0 0 0 2 2h14v-11H6a2 2 0 0 1-2-1z"/><circle cx="16.5" cy="14" r="1.2" fill="currentColor" stroke="none"/>)svg"},
        {QStringLiteral("cart"), R"svg(<path d="M3.5 4.5h2.5l2 10.5h10l2-7.5H7"/><circle cx="9.5" cy="19" r="1.4"/><circle cx="16.5" cy="19" r="1.4"/>)svg"},
        {QStringLiteral("dumbbell"), R"svg(<path d="M6.5 7.5v9M4 9.5v5M17.5 7.5v9M20 9.5v5M6.5 12h11"/>)svg"},
        {QStringLiteral("plant"), R"svg(<path d="M7.5 15h9l-1.3 5.5H8.8zM12 15V10.5M12 12.5c0-3.3-2.3-5.7-6-5.7 0 3.3 2.3 5.7 6 5.7zM12 10.5c0-3.1 2.2-5.5 5.8-5.5 0 3.1-2.2 5.5-5.8 5.5z"/>)svg"},
        {QStringLiteral("hourglass"), R"svg(<path d="M6.5 3.5h11M6.5 20.5h11M7.5 3.5c0 4.5 4.5 5.5 4.5 8.5S7.5 16 7.5 20.5M16.5 3.5c0 4.5-4.5 5.5-4.5 8.5s4.5 4 4.5 8.5"/>)svg"},
        {QStringLiteral("puzzle"), R"svg(<path d="M5 7.5h3.5a2 2 0 1 1 4 0H16v3.5a2 2 0 1 1 0 4V18.5h-3.5a2 2 0 1 0-4 0H5V15a2 2 0 1 0 0-4z"/>)svg"},
    };
    return b;
}

QByteArray svgFor(const QString &name, const QColor &color)
{
    const char *body = bodies().value(name, nullptr);
    if (!body)
        return {};
    return QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
                          "stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" color=\"%1\">%2</svg>")
        .arg(color.name(QColor::HexRgb), QString::fromLatin1(body))
        .replace(QStringLiteral("currentColor"), color.name(QColor::HexRgb))
        .toUtf8();
}

class TintEngine : public QIconEngine
{
public:
    TintEngine(const QString &name, const QColor &color)
        : m_name(name)
        , m_color(color)
    {
    }
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor c = m_color.isValid() ? m_color : Theme::colors().mist;
        if (mode == QIcon::Disabled)
            c = Theme::colors().dust;
        else if (mode == QIcon::Active || mode == QIcon::Selected)
            c = Theme::colors().starlight;
        Icons::paint(*p, rect, m_name, c);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(), size), mode, state);
        return pm;
    }
    QIconEngine *clone() const override { return new TintEngine(m_name, m_color); }

private:
    QString m_name;
    QColor m_color;
};
}

namespace Icons
{

bool has(const QString &name)
{
    return bodies().contains(name);
}

QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(name).arg(size).arg(color.rgba()).arg(dpr);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    const int px = qMax(1, qRound(size * dpr));
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QSvgRenderer r(svgFor(name, color));
    if (r.isValid()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        r.render(&p, QRectF(0, 0, px, px));
    }
    if (color.alpha() < 255) {
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.fillRect(pm.rect(), QColor(0, 0, 0, color.alpha()));
    }
    pm.setDevicePixelRatio(dpr);
    if (cache.size() > 2000)
        cache.clear();
    cache.insert(key, pm);
    return pm;
}

QIcon icon(const QString &name, const QColor &color)
{
    return QIcon(new TintEngine(name, color));
}

void paint(QPainter &p, const QRectF &rect, const QString &name, const QColor &color)
{
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : qApp->devicePixelRatio();
    const int size = qMax(1, qRound(qMin(rect.width(), rect.height())));
    const QPixmap pm = pixmap(name, size, color, dpr);
    const QRectF target(rect.center().x() - size / 2.0, rect.center().y() - size / 2.0, size, size);
    p.drawPixmap(target, pm, QRectF(pm.rect()));
}

QList<Group> pageGlyphGroups()
{
    return {
        {QApplication::translate("Icons", "Space"),
         {QStringLiteral("planet"), QStringLiteral("moon"), QStringLiteral("sun"), QStringLiteral("sparkle"),
          QStringLiteral("star"), QStringLiteral("rocket"), QStringLiteral("comet"), QStringLiteral("telescope"),
          QStringLiteral("galaxy"), QStringLiteral("orbit"), QStringLiteral("constellation"), QStringLiteral("eclipse"),
          QStringLiteral("satellite"), QStringLiteral("ufo"), QStringLiteral("astronaut"), QStringLiteral("globe")}},
        {QApplication::translate("Icons", "Work"),
         {QStringLiteral("page"), QStringLiteral("notebook"), QStringLiteral("book"), QStringLiteral("bookmark"),
          QStringLiteral("briefcase"), QStringLiteral("calendar"), QStringLiteral("check-circle"), QStringLiteral("target"),
          QStringLiteral("flag"), QStringLiteral("chart"), QStringLiteral("database"), QStringLiteral("inbox"),
          QStringLiteral("folder"), QStringLiteral("tag"), QStringLiteral("clock"), QStringLiteral("hourglass"),
          QStringLiteral("people"), QStringLiteral("chat"), QStringLiteral("mail"), QStringLiteral("pin"),
          QStringLiteral("code"), QStringLiteral("terminal"), QStringLiteral("key"), QStringLiteral("shield"),
          QStringLiteral("graduation"), QStringLiteral("wallet"), QStringLiteral("cart"), QStringLiteral("puzzle")}},
        {QApplication::translate("Icons", "Life"),
         {QStringLiteral("bulb"), QStringLiteral("heart"), QStringLiteral("music"), QStringLiteral("camera"),
          QStringLiteral("coffee"), QStringLiteral("leaf"), QStringLiteral("plant"), QStringLiteral("mountain"),
          QStringLiteral("compass"), QStringLiteral("fire"), QStringLiteral("bolt"), QStringLiteral("diamond"),
          QStringLiteral("palette"), QStringLiteral("film"), QStringLiteral("gamepad"), QStringLiteral("gift"),
          QStringLiteral("dumbbell"), QStringLiteral("home"), QStringLiteral("smile"), QStringLiteral("info")}},
    };
}

QStringList pageGlyphs()
{
    QStringList all;
    for (const Group &g : pageGlyphGroups())
        all += g.names;
    return all;
}

void paintPageIcon(QPainter &p, const QRectF &rect, const QString &icon, const QColor &color, const QString &fallback)
{
    QString glyph;
    if (icon.startsWith(QLatin1String("glyph:")))
        glyph = icon.mid(6);
    else if (icon.isEmpty())
        glyph = fallback;
    if (!glyph.isEmpty()) {
        paint(p, rect, glyph, color);
        return;
    }
    if (icon.isEmpty())
        return;
    // An uploaded image.
    if (icon.startsWith(QLatin1String("media:"))) {
        static QHash<QString, QImage> cache;
        const QString media = icon.mid(6);
        // Animated icons (GIFs) play.
        const QImage frame = AnimatedMedia::frame(media);
        if (!frame.isNull())
            AnimatedMedia::repaintOnNextFrame(p, rect, media);
        QImage img = frame.isNull() ? cache.value(media) : frame;
        if (img.isNull()) {
            if (Workspace *ws = Workspace::instance()) {
                QImageReader reader(ws->mediaPath(media));
                reader.setAutoTransform(true);
                if (reader.size().width() > 256)
                    reader.setScaledSize(reader.size().scaled(256, 256, Qt::KeepAspectRatio));
                img = reader.read();
            }
            if (cache.size() > 200)
                cache.clear();
            cache.insert(media, img);
        }
        if (img.isNull()) {
            paint(p, rect, fallback.isEmpty() ? QStringLiteral("image") : fallback, color);
            return;
        }
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(rect, rect.width() * 0.18, rect.height() * 0.18);
        p.setClipPath(clip);
        const QSizeF scaled = QSizeF(img.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
        p.drawImage(QRectF(rect.center().x() - scaled.width() / 2, rect.center().y() - scaled.height() / 2, scaled.width(), scaled.height()), img);
        p.restore();
        return;
    }
    // Emoji: size the glyph to the box.
    p.save();
    QFont f(QStringLiteral("Noto Color Emoji"));
    f.setPixelSize(qMax(6, qRound(qMin(rect.width(), rect.height()) * 0.86)));
    p.setFont(f);
    p.setPen(color);
    p.drawText(rect.adjusted(-4, -4, 4, 4), Qt::AlignCenter, icon);
    p.restore();
}

QList<Group> emojiGroups()
{
    auto split = [](const QString &s) { return s.split(QLatin1Char(' '), Qt::SkipEmptyParts); };
    return {
        {QApplication::translate("Icons", "Space & nature"),
         split(QStringLiteral("🪐 🌙 🌕 🌑 🌗 ☀️ ⭐ 🌟 ✨ 💫 ☄️ 🌌 🚀 🛸 🛰️ 🔭 👽 🌍 🌎 🌏 🌈 ⚡ 🔥 💧 🌊 ❄️ 🌋 🏔️ 🌲 🌵 🌸 🌻 🍀 🍁 🌱 🌿"))},
        {QApplication::translate("Icons", "Objects"),
         split(QStringLiteral("📝 📓 📔 📒 📕 📗 📘 📙 📚 📖 🔖 📌 📍 📎 🖇️ ✏️ 🖊️ 🖋️ 📐 📏 🗂️ 📁 📂 🗃️ 🗄️ 📊 📈 📉 📅 📆 🗓️ ⏰ ⏳ 💡 🔑 🔒 🧭 🗺️ 💼 🎒 🧪 🔬 💻 ⌨️ 🖥️ 📱 🎧 📷 🎬 🎨 🎵 🎮 🧩 🎯 🏆 🎁"))},
        {QApplication::translate("Icons", "Symbols"),
         split(QStringLiteral("✅ ☑️ ❌ ⚠️ ❗ ❓ 💬 💭 🔔 ♻️ ⚙️ 🔗 🧠 ❤️ 🖤 🤍 🩶 💜 💙 💚 🧡 💛 ⬛ ⬜ ◼️ ◻️ ▪️ ▫️ 🔘 ⚫ ⚪ 🔺 🔻 🔷 🔶 💠"))},
        {QApplication::translate("Icons", "Life"),
         split(QStringLiteral("🏠 🏡 🏢 ✈️ 🚲 🚗 ☕ 🍵 🍎 🍕 🥗 🍰 🏃 🧘 🏋️ ⚽ 🎾 🎲 🛒 💰 💳 🧾 👋 👍 🙌 👀 🤔 😊 😎 🥳 🤩 😴 🧑‍🚀 🐱 🐶 🦊 🦉 🐋"))},
    };
}

}
