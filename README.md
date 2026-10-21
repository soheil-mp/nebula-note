# Nebula Note

A calm, space-gray workspace for notes, documents, wikis and databases, written
in C++ with Qt 6, made to replace Notion on Linux. Everything lives in blocks you
can type, nest, drag, put side by side and turn into one another; databases are
pages full of pages that you can see as a table, board, list, gallery, calendar,
timeline, chart, feed or form.

![The welcome page](docs/screenshots/welcome.png)

It shares its look with [Umbra](../text), [Sidereus Docs](../docs),
[Ephemeris](../sheets), [Almagest](../pdf), [Orbit](../productivity) and
[Lunar](../lunar-slides): the palette is shades of gray from void `#09090B` to
starlight `#F1F1F5`. Stars drift behind a translucent sidebar, page covers are
procedural space scenes (nebulae, eclipses, ringed worlds, spiral galaxies,
moonrises…), and Notion's colors become gray **shades**.

| | |
|---|---|
| ![Board](docs/screenshots/board.png) | ![Table with sub-items and calculations](docs/screenshots/table.png) |
| ![Timeline with dependencies](docs/screenshots/timeline.png) | ![Calendar with date ranges](docs/screenshots/calendar.png) |
| ![Chart](docs/screenshots/chart.png) | ![Gallery](docs/screenshots/gallery.png) |
| ![Side peek](docs/screenshots/peek.png) | ![Inline database and linked view](docs/screenshots/inline-db.png) |
| ![Columns and a simple table](docs/screenshots/columns.png) | ![Equations, table of contents, breadcrumb](docs/screenshots/blocks.png) |
| ![Version history](docs/screenshots/history.png) | ![Quick Find with filters](docs/screenshots/quickfind.png) |
| ![Tabs](docs/screenshots/tabs.png) | ![Views in the sidebar](docs/screenshots/sidebar-views.png) |
| ![Commands](docs/screenshots/slash.png) | ![A YouTube video embedded in a page](docs/screenshots/video.png) |
| ![Home with the month calendar](docs/screenshots/home.png) | ![The page menu: fonts, text size, width](docs/screenshots/page-menu.png) |

## Install

```sh
./install.sh             # builds, installs to ~/.local and writes "Nebula Note.desktop" into this folder
./install.sh --desktop   # …and puts a launcher on the desktop too
./uninstall.sh           # removes it again (your notes are kept unless --purge)
```

Then **double-click `Nebula Note.desktop`** in this folder (or find Nebula Note in
the app menu). Launching it again while it is open just brings the window forward.
Installing over the earlier "Nebula" build removes its old launcher and menu
entry; your workspace stays where it was.

Build dependencies on Ubuntu/Debian:
`cmake ninja-build g++ qt6-base-dev qt6-svg-dev zlib1g-dev` and optionally
`libkf6windowsystem-dev` for native KWin shadows and blur behind the sidebar.
YouTube and Vimeo videos play inside pages with Qt WebEngine
(`qt6-webengine-dev`); when only its libraries are installed, `install.sh`
fetches the headers into `~/.cache/nebula-note` (no root needed). Without
WebEngine, video embeds show their thumbnail and open in the browser.
Spell check uses Hunspell dictionaries when they are installed (`hunspell-en-us`).

## Writing

- **Blocks**: text, headings 1–4 (any of them can be a toggle heading), bulleted,
  numbered (1. a. i. by depth) and to-do lists, toggles, quotes and callouts that
  hold other blocks, code with grayscale highlighting and captions, **Mermaid
  flowcharts** drawn in place, **block and inline equations** (a built-in TeX
  typesetter: fractions, roots, scripts, big operators, matrices, cases, accents,
  Greek and the usual symbols), dividers, images (caption, resize, align, full
  width, full screen), **YouTube and Vimeo embeds** that play in the page (paste
  the link), video/audio/file/PDF cards, web bookmarks with previews,
  buttons, synced blocks, table of contents, breadcrumb, links to pages and
  inline databases.
- **Columns**: drag a block to the left or right edge of another, or type
  `/2 columns` … `/5 columns`; drag the gap to resize. **Simple tables** with
  header row and column, Tab between cells, add rows/columns from the edges,
  cell shades, fit to width, and "turn into database".
- **`/` command menu** organized like Notion's (basic blocks, media, database,
  advanced, inline, colors, actions), with recent commands first; **`@`** for
  pages, dates ("tomorrow 3pm", "next friday", "remind me monday") and emoji; **`[[`** and **`+`** for page links and new sub-pages; **`:`** emoji
  autocomplete; `$$…$$` for inline math.
- **Markdown as you type**, block selection with **Esc** (arrows, Shift+arrows,
  Delete, Ctrl+D, Ctrl+/ for the block menu), drag-and-drop with guides, the
  `+` and `⋮⋮` handles, Turn into, Color, Copy link to block, Move to, word
  count.
- **Comments**: page discussions and comments on selected text (Ctrl+Shift+M),
  threads, resolve and reopen. **Spell check** with suggestions (it can be
  turned off in Settings).
- **Pages**: icons (glyphs, emoji or your image), covers (generated scenes or
  your image); a ••• menu like Notion's with the font as Default/Serif/Mono
  tiles, text size and page width as two-way switches that apply live, lock,
  an outline of the top-level headings in the margin, backlinks (listed, as a
  count or off), find in
  page and **version history** (a version every ten minutes of editing:
  preview, name, restore).
- **Paste** keeps structure: Markdown, rich text, images, `nebula://` links to
  pages and blocks; pasting a URL offers a link, bookmark or embed, and a video
  link embeds the video right away.

## Databases

- **Properties**: text, number (formats, bar or ring), select, multi-select,
  status (To-do / In progress / Complete groups), date (ranges, times,
  reminders, display formats), person, files & media, checkbox, URL, email,
  phone, **formula** (Notion's formula language: `prop()`, `if`/`ifs`, `let`,
  `map`/`filter`, dates, text, lists, with an editor that previews results),
  **relation** (one- or two-way, to the same or another database, limit to one
  page), **rollup** (every Notion calculation), created/edited time and by,
  unique ID with prefix and button. Properties take descriptions.
- **Views**: **table** (resizable, reorderable, frozen and wrapped columns, a
  calculation footer, row drag, tick boxes for bulk edit/duplicate/delete),
  **board** (group by any property, sub-groups as swimlanes, folded and hidden
  columns, colored columns, card size and previews), **list**, **gallery**,
  **calendar** (date ranges as spanning bars, weekends on or off, drag to move),
  **timeline** (day…year scales, drag bars and their ends, a table beside it,
  dependency arrows, unscheduled items), **chart** (vertical/horizontal bars,
  line, donut, number; stacking, sorting, cumulative, labels), **feed** and
  **form** (answers become rows; required questions).
- **Filters** with Notion's advanced builder: AND/OR groups nested three
  levels deep, relative dates (today, one week ago…), "is in status group".
  Sorts, search, grouping (by date bucket, alphabet, number range…) with
  hidden and empty groups, and per-view layout options.
- **Sub-items** (nested in toggles, flattened, parents only) and
  **dependencies**; row **templates** (default template, preset properties and
  content) behind the New ▾ button; **lock database**.
- Rows open in a **side peek**, **center peek** or as a full page. Databases can
  live **inline** in a page, and **linked views** show a database elsewhere with
  their own filters and layouts.

## Finding your way

- **Sidebar** with Favorites and the page tree (drag to reorder or nest;
  databases list their views).
- **Quick Find** (Ctrl+P / Ctrl+K): fuzzy titles and page text, with Notion's
  filters — title only, in a page, edited/created date — and sorts.
- **Reminders** (`@remind friday 9am`) pop up as notifications when they come due.
- **Home**: recent pages, quick starts, everything dated in the next two weeks
  and a month calendar marking the days that have something on them.
- **Tabs** (Ctrl+T, Ctrl+W, Ctrl+Tab) and **new windows** (Ctrl+Shift+N); back
  and forward, breadcrumbs, Ctrl+Shift+U for the parent page.

## Your data

Your workspace is a single SQLite file, `~/.local/share/nebula/workspace.db`,
with media in `media/` beside it. Nothing leaves your computer except link
previews you ask for. Edits save automatically; a copy is made once a day into
`backups/` (the last seven are kept), and deleted pages wait in the **Trash**.

- **Import**: Notion exports (*Markdown & CSV* or *HTML*, zipped or not — pages,
  sub-pages, callouts with their content, columns with their widths, inline
  databases with typed properties, links between pages), Markdown and HTML files
  or folders (Obsidian `[[links]]` included), CSV, **Word documents (.docx)** and
  **Evernote exports (.enex)**. Every attachment is **stored in your workspace**:
  images, PDFs, videos and other files that came with the export are copied into
  `media/`, and images that live on the web (page covers, icons, pictures linked
  by URL) are downloaded during the import, so nothing depends on the export or
  on Notion afterwards.
- **Export**: any page to Markdown (Notion-style for columns, tables,
  equations and toggle headings; sub-pages and databases as CSV), HTML or PDF
  (equations and diagrams are rendered), or the whole workspace from Settings.
  From the command line: `nebula-note --open "Page title" --export page.pdf`.

## Keyboard

| Keys | Action |
|---|---|
| Ctrl+P, Ctrl+K | Quick Find |
| Ctrl+N | New page |
| Ctrl+T / Ctrl+W / Ctrl+Tab | New tab / close tab / next tab |
| Ctrl+Shift+N | New window |
| Ctrl+[ / Ctrl+] | Back / forward |
| Ctrl+\\ | Toggle the sidebar |
| Ctrl+F | Find in page |
| Ctrl+, | Settings |
| / | Command menu |
| @, [[, + | Mention a page or date, link a page, new sub-page |
| Esc | Select the block (arrows, Delete, Ctrl+D, Ctrl+/) |
| Ctrl+B · I · U · E | Bold · italic · underline · inline code |
| Ctrl+Shift+S · E · M · H | Strikethrough · equation · comment · last color |
| Ctrl+Shift+0 … 9 or Ctrl+Alt+0 … 9 | Turn into text, H1–H3, to-do, bullets, numbers, toggle, code, page |
| Tab / Shift+Tab | Nest / un-nest |
| Ctrl+Shift+↑ / ↓ | Move the block |
| Ctrl+Enter | Check a to-do, open a toggle, open a page link |
| Ctrl+Alt+T | Expand or collapse every toggle |

## Building and testing

```sh
cmake -S . -B build -G Ninja && cmake --build build
cd build && ctest --output-on-failure
```

Seven suites run offscreen:

- `nebula_tests`: block model, Markdown, CSV type inference, filters and sorts,
  the workspace, zip reading, Notion and Obsidian import, exports.
- `nebula_db_tests`: formulas, relations and rollups, groups, sub-items,
  templates, reminders, comments, versions, synced blocks.
- `nebula_io_tests`: Markdown round trips of the rich blocks, HTML (and Notion
  HTML export) import, Word and Evernote import, HTML/PDF export.
- `nebula_editor_tests` and `nebula_blocks_tests`: Markdown shortcuts,
  Enter/Backspace rules, nesting, toggles, columns, simple tables, equations,
  dates, block selection, drag and drop, synced blocks, comments, copy/paste.
- `nebula_dbui_tests`: every database layout driven with real mouse events —
  groups and calculations, bulk selection, sub-items, frozen and reordered
  columns, board drags across groups and lanes, calendar ranges, timeline
  resizing, charts, forms, the filter builder, settings, linked views, load
  more, inline databases in pages and the row peek.
- `nebula_app_tests`: the real window — every page and view, trash, overlays,
  version history, the page menu and customisation, reminders, search filters,
  views in the sidebar, tabs, new windows, saving on close.

`docs/NOTION_PARITY.md` lists Notion's features one by one and how Nebula Note
covers them; `docs/RESEARCH.md` describes the research and design decisions.

## Licence notes

Inter is bundled under the SIL Open Font License (`resources/fonts/OFL-Inter.txt`).
The video in the embed screenshot is *Big Buck Bunny* (© Blender Foundation,
CC BY 3.0).
Icons and cover art are drawn by Nebula Note itself.
