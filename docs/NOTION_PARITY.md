# Notion feature parity

A feature-by-feature comparison with Notion, built from Notion's help centre
(writing & editing basics, columns/headings/dividers, simple tables, code
blocks, math equations, media, buttons, synced blocks, comments & mentions,
version history, search, database properties, views/filters/sorts, tables,
boards, timelines, charts, forms, formulas, relations & rollups, templates,
sub-items & dependencies) and Notion's API block reference and "enhanced
Markdown" spec, which list every block type and attribute.

Status: ✅ done · ◐ partial (what's missing is noted) · ✗ not available (needs a
server, accounts, a web engine, AI or a map service — or is out of scope).

Most of this is covered by the test suites (`ctest`), which drive the editor,
the database layouts and the window offscreen; the README lists what each suite
exercises. Purely visual details (covers, pickers, animations) are checked by
screenshots instead.

## Blocks

| Notion | Behaviour | Nebula Note |
|---|---|---|
| Text | default block, colour, children | ✅ |
| Page / sub-page | nested pages, Turn into page | ✅ |
| To-do | checkbox, children, Ctrl+Enter | ✅ |
| Heading 1–4 | `#`…`####`, colours | ✅ |
| Toggle heading 1–4 | collapsible headings with children | ✅ |
| Bulleted / numbered list | `-` `*` `+`, `1.` `a.` `i.`, nested numbering styles | ✅ |
| Toggle list | `>`, open/close, children | ✅ |
| Quote | `"`, children | ✅ |
| Callout | icon, colour, holds several blocks | ✅ |
| Divider | `---` | ✅ |
| Link to page | page reference block | ✅ |
| Columns | drag beside a block, `/2 columns`…`/5 columns`, resize, unwrap when one is left | ✅ |
| Simple table | `/table`, header row/column, add/remove rows & columns, cell shades, fit to width, Tab between cells, turn into database | ✅ |
| Code | language, caption, wrap, copy | ✅ |
| Mermaid diagrams | rendered preview | ◐ flowcharts (`graph`/`flowchart` TD/LR/…); other diagram types show the code |
| Block equation | `/math`, `$$` block, KaTeX syntax | ✅ built-in TeX typesetter (common KaTeX commands) |
| Inline equation | `$$…$$`, Ctrl+Shift+E | ✅ |
| Table of contents | live list of headings, click to jump | ✅ |
| Breadcrumb | page path | ✅ |
| Button | insert blocks, add page to database, open page/URL, confirmation | ✅ |
| Synced block | turn into synced block, copy/paste references, unsync, "Editing in N pages" | ✅ |
| Image | upload/paste/drop, caption, resize, align, full width, full screen, replace, download | ✅ |
| Video / Audio / File / PDF | upload or link, caption | ◐ shown as cards that open in the system player/viewer (no inline playback) |
| Web bookmark | link preview card | ✅ title/description fetched when online |
| Embeds (YouTube, Figma…) | iframe embeds | ✅ YouTube and Vimeo play in the page (Qt WebEngine; a thumbnail that opens the browser without it), pasting a video link embeds it; other sites show as cards |
| Inline database / linked view | `/table view`…, `/linked view of database` | ✅ live databases inside pages; linked views keep their own views |
| Template button (legacy) | insert template | ✅ (a button that inserts blocks) |
| Tabs block, meeting notes, AI blocks | | ✗ |

## Editing

| Notion | Nebula Note |
|---|---|
| `/` menu with search, sections, recent items first, colour commands (`/red`, `/gray background`) | ✅ colours map to gray shades |
| `+` and `⋮⋮` handles, drag & drop with guides, drag into columns | ✅ |
| Block menu: Turn into, Colour, Copy link to block, Duplicate, Move to, Delete, Comment, word & character count | ✅ |
| Block selection: Esc, Shift+↑/↓, drag in margin, Delete, Ctrl+D, Ctrl+/ | ✅ |
| Markdown shortcuts (block and inline), `$$` equations | ✅ |
| Keyboard shortcuts (Ctrl+Shift+0…9, Ctrl+Alt+0…9, Ctrl+Shift+H repeat colour, Ctrl+Shift+M comment, Ctrl+Shift+E equation, Ctrl+Alt+T toggles, Ctrl+Enter) | ✅ |
| `@` mentions: pages, dates (today, tomorrow, next Friday…), reminders | ✅ (no people mentions: single user) |
| `[[` and `+` page links / new sub-page | ✅ |
| `:` emoji autocomplete, `/emoji` | ✅ |
| Paste URL → bookmark / mention / link | ✅ |
| Comments: page discussions, inline comments, threads, resolve/reopen | ✅ |
| Spell check with suggestions | ✅ Hunspell, when a dictionary is installed |
| Find in page | ✅ |
| Suggest edits, Ask AI | ✗ |

## Pages

| Notion | Nebula Note |
|---|---|
| Icon (emoji, icons, upload), cover (gallery, upload, reposition) | ✅ |
| Font Default/Serif/Mono, small text, full width, lock | ✅ |
| Customize page: table of contents in the margin, page discussions, backlinks expanded / in a popover / off | ✅ |
| Version history (a version every 10 minutes of editing), preview, name, restore | ✅ |
| Duplicate, Move to, Copy link, Export (Markdown & CSV, HTML, PDF), Import | ✅ |
| Updates / analytics, sharing, publish to web, teamspaces, guests, permissions | ✗ single user, local only |

## Databases

| Notion | Nebula Note |
|---|---|
| Properties: text, number (formats, bar/ring), select, multi-select, status, date (range, time, reminder, formats), person, files & media, checkbox, URL, email, phone, formula, relation (one/two-way, self, limit one), rollup, created/edited time & by, button, unique ID | ✅ (person and "by" use a local list of people) |
| Property descriptions | ✅ |
| Place, verification | ✗ |
| Views: table, board, list, gallery, calendar, timeline, chart (bar, horizontal bar, line, donut, number), feed, form | ✅ |
| Chart settings: X/Y "what to show" (Name for a bar per page; count, sum, average, median, min, max), decimal places, group by, range, reference lines, sort, omit zero values, color, height, grid lines, axis names, data labels, legend, caption, smooth line, save as PNG/SVG | ✅ (Notion's export has no views: charts are set up again after an import) |
| Form view | ◐ answers become rows, required questions; no shareable link (local only) |
| Map view | ✗ no map tiles |
| Filters incl. advanced AND/OR groups (3 levels), relative dates, sorts, search | ✅ |
| Group (dates by day/week/month/year/relative, text alphabetically, number ranges), hide empty groups, hidden and folded groups | ✅ |
| Sub-groups | ◐ board swimlanes; tables and lists have one level of groups |
| Table: calculations footer, wrap cells, freeze columns, column reorder/resize, row drag, bulk select, edit, duplicate, delete | ✅ |
| Layout: open in side peek / center peek / full page, card preview (cover, content, files) & size, fit image, colour columns, show weekends, load limit, page icons | ✅ |
| Database templates (default, preset properties, content) | ✅ |
| Sub-items (nested toggles, flattened, parents only) | ✅ |
| Dependencies ("Blocked by" / "Blocking") | ◐ arrows on the timeline; dates don't shift automatically |
| Inline databases, linked views, lock database | ✅ |
| Automations, AI autofill, chart dashboards | ✗ |

## Workspace

| Notion | Nebula Note |
|---|---|
| Sidebar: favourites, page tree, drag to nest, database views listed, trash | ✅ |
| Search: title only, in page, edited/created date filters, sort by best match / edited / created | ✅ |
| Home: recents, upcoming, a month calendar | ✅ |
| Inbox | ➖ left out on purpose; reminders still notify (desktop notifications via `notify-send` when available) |
| Tabs (Ctrl+T, Ctrl+W, Ctrl+Tab), new window (Ctrl+Shift+N) | ✅ |
| Import: Notion export (Markdown & CSV, HTML), Markdown, CSV, HTML, Word (.docx), Evernote (.enex) | ✅ attachments (images, PDFs, files) are copied into the workspace and web images (covers, icons, linked pictures) downloaded; inline databases come back live (Word: no comments, footnotes or tracked changes) |
| Templates gallery | ➖ left out on purpose (database row templates remain) |
| Light/dark mode | ✗ by design (the look is space gray) |
| Offline, desktop app | ✅ it only ever works locally |
| Sync across devices, mobile apps, web app | ✗ |
