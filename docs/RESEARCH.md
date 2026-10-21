# Nebula Note — research & implementation plan

Nebula Note (first released as Nebula) is a native Qt 6 / C++ workspace for notes, documents, wikis and
databases, meant as a day-to-day replacement for Notion. It shares the "deep
space in grayscale" design of its sibling apps (Umbra, Ephemeris, Almagest,
Orbit, Sidereus, Lunar): every colour is a gray from void `#09090B` to
starlight `#F1F1F5`, and meaning comes from brightness, weight and shape.

This document records the research done before writing code and the decisions
that came out of it.

---

## 1. What a Notion replacement has to cover

Notion's own help pages (keyboard shortcuts, database properties, database
views) and its engineering post on the block data model describe the surface
area. The core of daily use is:

| Area | Notion | Nebula scope |
| --- | --- | --- |
| Pages | nested pages, icon, cover, title, favourites, trash, full width, small text, Default/Serif/Mono font | ✅ all of it |
| Blocks | text, H1–H3, bulleted / numbered / to-do / toggle lists, quote, callout, divider, code, image, sub-page, link to page | ✅ all of these |
| Inline | bold, italic, underline, strike, inline code, link, text colour + highlight, page mentions (`@`, `[[`) | ✅ (colours become gray shades) |
| Editing | `/` command menu, Markdown shortcuts, block handle (⋮⋮) with drag & drop, Turn into, Tab/Shift+Tab nesting, Ctrl+Shift+0…9 block shortcuts, Ctrl+D duplicate, Ctrl+Shift+↑/↓ move | ✅ |
| Navigation | sidebar tree, Quick Find (Ctrl+P / Ctrl+K), back / forward (Ctrl+[ / Ctrl+]), breadcrumbs, backlinks | ✅ plus a Home dashboard |
| Databases | properties (text, number, select, multi-select, status, date, checkbox, URL, email, phone, created / edited time), rows are pages | ✅ these 12 types; relation, rollup, formula, person and files are left out |
| Views | table, board, list, gallery, calendar, timeline; filter, sort, group, per-view settings | ✅ table, board, list, gallery, calendar with filter, sort and grouping; timeline left out |
| Sharing | export Markdown / HTML / PDF, import Markdown, CSV, Notion export | ✅ including the Notion "Markdown & CSV" zip |
| Templates | template gallery, duplicate | Duplicate and database row templates (the page template gallery was left out) |

Real-time multi-user collaboration, comments, AI and web embeds are left out on
purpose. They need a server, an account system or a web engine. The Qt
installation on this machine has no QtWebEngine.

Sources: Notion Help Center: *Keyboard shortcuts*, *Database properties*,
*Using database views*, *Export your content*; Notion engineering blog: *The
data model behind Notion's flexibility*.

## 2. Data model

Notion's model is "everything is a block". A block has an id, a type, a
property bag, an ordered `content` array of child ids and a `parent` pointer.
Three details from the engineering post shaped Nebula's model:

1. **Indentation is structure, not style.** Indenting moves a block into the
   preceding sibling's children. Nebula stores a `depth` on each block in a
   page's flat list, and a block's children are the blocks after it with a
   greater depth. Tab and Shift+Tab move a block together with its subtree,
   and so does dragging.
2. **"Turn into" changes only the type.** Properties the new type doesn't use
   are kept, so a checked to-do turned into a heading and back is still
   checked. Nebula's block JSON keeps every property for the same reason.
3. **Database rows are pages with properties.** A row is a page whose parent
   is the database. It has its own block content, and its property values sit
   in a JSON object keyed by property id.

Storage is a single SQLite file (`~/.local/share/nebula/workspace.db`, WAL
mode):

```
pages(id, parent_id, kind[page|database|row], title, icon, cover, cover_pos,
      content JSON, plain, props JSON, schema JSON, options JSON,
      sort, favorite, created, updated, visited, trashed)
links(src, dst)              -- page mentions and sub-page links, for backlinks
meta(key, value)             -- schema version, workspace name, …
```

Page content is one JSON array per page rather than one row per block. A
single user doesn't need block-level sync, and one row per page makes saving
atomic and loading a single query. `plain` holds the page text for search. The
`links` table is rebuilt on each save so backlinks are a single indexed query.
Images are stored once, content-addressed, in `media/<sha1>.<ext>`.

Saving is automatic: edits are debounced (~600 ms) and written in one
transaction. A rolling daily backup (`VACUUM INTO`, keep 7) protects against
mistakes, and the trash keeps deleted pages until it is emptied.

## 3. Editor architecture

**One `QTextEdit` per page with block semantics on top, not one widget per
block.**

* A stack of per-block widgets (the literal Notion DOM approach) breaks text
  selection across blocks, needs custom caret movement between widgets and
  gets slow on long pages.
* One `QTextDocument` gives cross-block selection, undo/redo, IME, clipboard
  and accessibility for free. Block types live in `QTextBlockFormat` user
  properties, and the editor adds the Notion behaviour around them.

Prototypes on Qt 6.10.2 (this machine) checked the three risky parts before
any code was written:

| Question | Result |
| --- | --- |
| Can toggle children be hidden in a `QTextEdit`? Old Qt 4 forum threads say `QTextBlock::setVisible` only works in `QPlainTextEdit`. | **Works in Qt 6.10.** `setVisible(false)` + `markContentsDirty()` gives the blocks zero height. The caret can still step into a hidden block, so the editor moves it on to the next visible one. |
| Can custom backgrounds (callouts, code) be painted under the text? | **Yes.** Paint them in `paintEvent` before calling `QTextEdit::paintEvent`, with the viewport's auto-fill turned off. |
| Can cover, icon and title live inside the editor's scrolling area? | **Technically yes, rejected.** A viewport child in the root frame's top margin scrolls with the document. But changing a frame or block format is recorded in Qt's undo stack, so resizing the window or adding a cover would pollute undo. |
| Can a divider span the column? | **Yes.** A `QTextObjectInterface` handler whose `intrinsicSize` is the text width. |
| Do neighbouring block margins add up? | **No: Qt collapses them** (the larger wins), ignores the first block's top margin and excludes margins from block rects. See point 3 below. |

Key design points:

1. **The page is a column, the editor grows.** A scroll area holds the cover,
   icon, a separate title field, the row property panel, an auto-height block
   editor and the backlinks. Enter and ↓ in the title move into the body; ↑ and
   ← at the start of the body move back. Nothing that depends on window size is
   ever written into the document.
2. **Markers are painted, not typed.** Bullets, numbers, checkboxes, toggle
   arrows, the quote bar and the callout icon are drawn in the block's left
   margin. Clicks there are hit-tested, which keeps the text model clean and
   lets markers animate.
3. **Visual style is derived from semantics.** Character formats carry only
   inline meaning (bold, italic, code, link, mention, shade). Font size and
   weight come from the block type, so Turn into restyles a block without
   losing inline formatting. Because margins collapse, all spacing lives in
   *top* margins computed from the previous block (so two code blocks keep both
   paddings). A normalisation pass after each edit fixes margins and restyles
   merged text, joined into the same undo step, so undo stays consistent.
4. **Inline objects** (`QTextObjectInterface`): dividers, sub-page / link-to-page
   blocks and `@` mentions are object characters that store a page id. They
   always draw the page's current title and icon, so renaming a page updates
   every mention.
5. **Code blocks** are a single text block with U+2028 line separators. Enter
   inserts a line, and a monochrome `QSyntaxHighlighter` that only touches code
   blocks adds highlighting through layout formats, which are never saved.
6. **Collapsed state** of toggles lives in `QTextBlock::userState`, outside
   the undo stack, so opening a toggle never shows up in undo.
7. **Popups never steal focus.** The slash menu, mention picker and
   formatting bar are overlay widgets inside the main window. The editor keeps
   the caret and forwards ↑ ↓ Enter Esc to the open menu.

Typography follows Notion's reading proportions: a centred column about
720 px wide (full width optional), 16 px body text at 1.5 line height,
headings at 1.875 / 1.5 / 1.25 em, a 40 px title, and a cover about 30 % of the
window height with the icon overlapping its bottom edge. These values were
taken from common measurements of Notion pages. There is no official spec.

## 4. Databases

A database page has a schema (properties with options, views) stored as JSON,
and its rows are pages. The view widgets are built for the data:

* **Table**: `QTableView` with a custom model and delegates. Pills for
  select / status, inline editors, resizable columns, a "+ New" row and a "+"
  column header to add properties. Type-specific editors open as popovers.
* **Board**: custom-painted columns grouped by a select or status property.
  Cards can be dragged between columns, which updates the property.
* **List**, **Gallery** (cards with a cover preview), **Calendar** (month
  grid on a date property, with drag to reschedule).
* Every view has its own filters (AND of rules such as *is*, *contains*,
  *is empty*, *before*), sorts and visible properties. A row opens as a full
  page whose property panel sits under the title.

Select colours become **shades**. Ten pill styles range from a dark pill with
light text to a light pill with dark text, plus outlined variants, so tags
stay distinguishable without colour.

## 5. Import & export

The Notion *Markdown & CSV* export is a zip. Pages are `Title <32-hex id>.md`,
and sub-pages sit in a folder named like the page. A database is
`Name <id>.csv` with a folder holding one `.md` per row. Nebula reads the zip
directly with zlib, strips the ids and rebuilds the hierarchy:

* Markdown is converted to blocks: headings, lists, `- [ ]` to-dos, quotes,
  fences, dividers, images, and `<aside>` (Notion's callouts) and
  `<details>` (toggles).
* CSV becomes a database. Column types are inferred: checkbox (Yes / No),
  number, date, select (few distinct values), multi-select (comma lists),
  URL, email or text. Rows are matched to their `.md` page by title.
* Links between exported pages are rewritten to page mentions.

Export goes to Markdown (a single page, or a folder tree with CSVs for
databases), HTML (self-contained) and PDF (`QPdfWriter`).

## 6. Look & feel

* **Palette** shared with the sibling apps: void, abyss, basalt, slate,
  graphite, ash, smoke, dust, fog, haze, mist, moon, starlight.
* **Space layer**: a faint seamless starfield behind the sidebar and the
  window chrome. Covers are procedural space scenes drawn in grayscale
  (nebula, eclipse, planet rise, ringed planet, spiral galaxy, lunar surface,
  orbits, aurora, comet, deep field), so they are crisp at any size and cost
  no disk space. Image covers are also supported.
* **Frameless window** with a soft KWin shadow (KWindowSystem), native move
  and resize through `startSystemMove` / `startSystemResize`, and optional
  blur behind a translucent sidebar.
* **Motion**: page crossfade, sidebar slide, toggle arrow rotation, and a
  star-spark burst when a to-do is checked. *Reduce motion* turns it all off.
* **Icons**: a monochrome line icon set drawn for Nebula (UI and page icons),
  plus the system emoji font for page icons.
* **Fonts**: Inter (bundled, OFL) for UI and text, Inter Display for titles,
  a serif option (PT Serif / Noto Serif / DejaVu Serif) and a mono option
  (JetBrains Mono / DejaVu Sans Mono).

## 7. Launching

KDE Plasma launches an executable `.desktop` file on double-click. GNOME-style
desktops also want the `metadata::trusted` flag. `install.sh` builds a release
binary, installs it under `~/.local` (program, icons, menu entry), and writes
**`Nebula.desktop`** into the project folder. That is the single file icon you
double-click to open the app. `--desktop` also puts a copy on the desktop.
A second launch raises the running window instead of opening a second
instance on the same database.

## 8. Testing

* `nebula_tests` (QtTest, offscreen): block JSON round-trip, Markdown
  import/export, CSV type inference, database filters and sorts, workspace CRUD,
  trash and restore, backlinks, the zip reader, Notion import.
* `nebula_editor_tests`: Markdown shortcuts, Enter/Backspace rules for lists,
  toggle collapse, indent/outdent of subtrees, document ↔ blocks round-trip.
