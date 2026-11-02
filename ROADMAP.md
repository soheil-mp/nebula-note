# Roadmap

Nebula Note 1.2 covers most of what a single person uses Notion for, offline.
What's left is getting it into people's hands, finishing the features that
`docs/NOTION_PARITY.md` marks as partial, and making sure it holds up on big
workspaces and untrusted files. The estimates add up to about 100 hours.

## 1. Releases and packaging (~16 h)

- [ ] **CI** (4 h): GitHub Actions builds with CMake and Ninja on Ubuntu and
      runs the seven `ctest` suites offscreen on every push and pull request.
- [ ] **Tagged releases** (4 h): a tag builds a Flatpak bundle and attaches it
      to a GitHub release, with notes taken from the AppStream `<releases>`;
      start a `CHANGELOG.md` from the 1.1 and 1.2 notes.
- [ ] **Flathub** (6 h): add the OARS `<content_rating>` the metainfo still
      lacks, pass `flatpak-builder-lint`, review the permissions (network is
      only for link previews, videos and web images), and submit.
- [ ] **Other distributions** (2 h): check `install.sh`'s dependency hints on
      Fedora and Arch and fix what's wrong.

## 2. Finish the partial features (~34 h)

These are the ◐ rows in `docs/NOTION_PARITY.md`.

- [ ] **More Mermaid diagrams** (12 h): sequence, state and pie diagrams drawn
      in place like flowcharts, in the same grayscale style, and in PDF and
      HTML exports. Other types keep showing their code.
- [ ] **Inline media** (10 h): local audio and video play inside the page
      (Qt Multimedia, loaded as a plugin like the web player), and PDF blocks
      show their pages in place (Qt PDF) instead of only opening the system
      viewer.
- [ ] **Sub-groups in tables and lists** (6 h): a second level of grouping,
      as boards already have with swimlanes, with collapsed and hidden
      sub-groups and per-group calculations.
- [ ] **Dependencies that move dates** (6 h): moving an item that blocks
      others shifts their dates by the same amount, with one undo step and a
      per-database switch to turn it off.

## 3. New blocks and database features (~22 h)

- [ ] **Tabs block** (6 h): a block whose tabs each hold other blocks; it
      round-trips through Markdown export and Notion import.
- [ ] **Database automations** (12 h): local rules of the form *when a row
      is added or a property changes, set a property, apply a template or show
      a reminder*. Rules are edited in the database's settings, run inside the
      app, and are undoable.
- [ ] **Dashboards** (4 h): a page layout that places several charts and
      linked views side by side, sized in columns, the missing "chart
      dashboards" row.

## 4. Robustness and speed (~18 h)

- [ ] **Large workspaces** (8 h): a generator for a workspace with 10,000
      pages and 50,000 database rows; measure start-up, Quick Find, opening a
      big table and scrolling a board; fix the slow spots; keep the
      measurements as a `ctest` target with time budgets.
- [ ] **Fuzz the importers** (6 h): libFuzzer targets for the zip reader and
      the Notion, HTML, Word, Evernote, Markdown and CSV importers, since they
      read files from elsewhere; fix every crash and add it as a test.
- [ ] **Sanitizers in CI** (2 h): an AddressSanitizer and
      UndefinedBehaviorSanitizer build that runs the test suites.
- [ ] **Smaller source files** (2 h): split the largest files
      (`PropertyEditors.cpp`, `Workspace.cpp`, `Schema.cpp`, `Formula.cpp`)
      along the lines they already have.

## 5. Translations and accessibility (~10 h)

- [ ] **Translations** (6 h): the UI already goes through `tr()` (about 1,100
      strings) but has no `.ts` files. Generate them, load the system locale's
      translation at start-up, use Qt's `%n` plurals, and ship a first complete
      translation.
- [ ] **Accessibility audit** (4 h): go through every view with Orca and the
      keyboard and fix what can't be reached, starting with the custom-drawn
      parts: timeline and calendar bars, chart values and the cover and icon
      pickers.

## Not planned

Sharing, accounts, sync across devices, a web or mobile app, AI features and a
light theme are out of scope: Nebula Note is a single-user, local-only app with
one look.
