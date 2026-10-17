#include "Templates.h"

#include "Ids.h"
#include "Markdown.h"
#include "Schema.h"
#include "Workspace.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonArray>
#include <QLocale>

namespace Templates
{

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("Templates", s);
}

QString longDate(const QDate &d)
{
    return QLocale(QLocale::English).toString(d, QStringLiteral("dddd, MMMM d, yyyy"));
}

// Markdown → blocks, with callout icons set from "glyph:" markers.
QList<Block> md(const QString &text, const QStringList &calloutIcons = {})
{
    QList<Block> blocks = Markdown::toBlocks(text);
    int n = 0;
    for (Block &b : blocks)
        if (b.type == BlockType::Callout && b.icon.isEmpty())
            b.icon = QStringLiteral("glyph:") + calloutIcons.value(n++, QStringLiteral("bulb"));
    return blocks;
}

Block pageLink(const QString &id)
{
    Block b;
    b.type = BlockType::PageLink;
    b.pageId = id;
    return b;
}

Block mention(const QString &before, const QString &id, const QString &after)
{
    Block b;
    Span a;
    a.text = before;
    Span m;
    m.text = QString(QChar::ObjectReplacementCharacter);
    m.mention = id;
    Span c;
    c.text = after;
    b.spans = normalizeSpans({a, m, c});
    return b;
}

Option opt(const QString &id, const QString &name, int shade, int group = 0)
{
    Option o;
    o.id = id;
    o.name = name;
    o.shade = shade;
    o.group = group;
    return o;
}

Property prop(const QString &id, const QString &name, PropType type, const QList<Option> &options = {})
{
    Property p;
    p.id = id;
    p.name = name;
    p.type = type;
    p.options = options;
    return p;
}

Property statusProp(const QString &todo = {}, const QString &doing = {}, const QString &done = {})
{
    return prop(QStringLiteral("status"), tr("Status"), PropType::Status,
                {opt(QStringLiteral("todo"), todo.isEmpty() ? tr("Not started") : todo, 2, 0),
                 opt(QStringLiteral("doing"), doing.isEmpty() ? tr("In progress") : doing, 5, 1),
                 opt(QStringLiteral("done"), done.isEmpty() ? tr("Done") : done, 8, 2)});
}

Schema taskSchema()
{
    Schema s;
    s.props = {prop(QStringLiteral("title"), tr("Name"), PropType::Title), statusProp(),
               prop(QStringLiteral("priority"), tr("Priority"), PropType::Select,
                    {opt(QStringLiteral("high"), tr("High"), 8), opt(QStringLiteral("med"), tr("Medium"), 5), opt(QStringLiteral("low"), tr("Low"), 1)}),
               prop(QStringLiteral("due"), tr("Due"), PropType::Date),
               prop(QStringLiteral("tags"), tr("Tags"), PropType::MultiSelect,
                    {opt(QStringLiteral("design"), tr("Design"), 3), opt(QStringLiteral("eng"), tr("Engineering"), 6),
                     opt(QStringLiteral("writing"), tr("Writing"), 2), opt(QStringLiteral("research"), tr("Research"), 9)}),
               prop(QStringLiteral("owner"), tr("Owner"), PropType::Text)};
    Property estimate = prop(QStringLiteral("estimate"), tr("Estimate"), PropType::Number);
    estimate.description = tr("Working days the task should take.");
    Property left = prop(QStringLiteral("left"), tr("Days left"), PropType::Formula);
    left.formula = QStringLiteral("if(prop(\"Status\") == \"Done\", \"Done\", if(empty(prop(\"Due\")), \"\", "
                                  "let(d, dateBetween(dateEnd(prop(\"Due\")), now(), \"days\"), "
                                  "if(d < 0, \"Overdue\", if(d == 0, \"Due today\", if(d == 1, \"1 day\", format(d) + \" days\"))))))");
    left.formulaType = QStringLiteral("text");
    s.props += {estimate, left};
    View board = Schema::makeView(s, ViewType::Board, tr("Board"));
    board.groupBy = QStringLiteral("status");
    board.hidden = {QStringLiteral("owner"), QStringLiteral("status"), QStringLiteral("estimate"), QStringLiteral("left")};
    View table = Schema::makeView(s, ViewType::Table, tr("All tasks"));
    table.sorts = {{QStringLiteral("due"), false}};
    table.calcs = {{QStringLiteral("title"), QStringLiteral("count")}, {QStringLiteral("estimate"), QStringLiteral("sum")}};
    View timeline = Schema::makeView(s, ViewType::Timeline, tr("Roadmap"));
    timeline.dateProp = QStringLiteral("due");
    timeline.timelineTableProps = {QStringLiteral("status")};
    timeline.timelineScale = QStringLiteral("month");
    View cal = Schema::makeView(s, ViewType::Calendar, tr("Calendar"));
    cal.dateProp = QStringLiteral("due");
    View chart = Schema::makeView(s, ViewType::Chart, tr("Workload"));
    chart.chartType = QStringLiteral("bar");
    chart.chartX = QStringLiteral("priority");
    chart.chartGroup = QStringLiteral("status");
    chart.chartY = QStringLiteral("sum:estimate");
    s.views = {board, table, timeline, cal, chart};
    return s;
}

Schema readingSchema()
{
    Schema s;
    s.props = {prop(QStringLiteral("title"), tr("Title"), PropType::Title),
               prop(QStringLiteral("author"), tr("Author"), PropType::Text),
               statusProp(tr("To read"), tr("Reading"), tr("Finished")),
               prop(QStringLiteral("rating"), tr("Rating"), PropType::Select,
                    {opt(QStringLiteral("r5"), QStringLiteral("★★★★★"), 8), opt(QStringLiteral("r4"), QStringLiteral("★★★★"), 6),
                     opt(QStringLiteral("r3"), QStringLiteral("★★★"), 4), opt(QStringLiteral("r2"), QStringLiteral("★★"), 2)}),
               prop(QStringLiteral("genre"), tr("Genre"), PropType::MultiSelect,
                    {opt(QStringLiteral("scifi"), tr("Science fiction"), 7), opt(QStringLiteral("science"), tr("Science"), 3),
                     opt(QStringLiteral("classic"), tr("Classic"), 9), opt(QStringLiteral("essay"), tr("Essays"), 1)}),
               prop(QStringLiteral("finished"), tr("Finished on"), PropType::Date)};
    View gallery = Schema::makeView(s, ViewType::Gallery, tr("Shelf"));
    gallery.hidden = {QStringLiteral("finished"), QStringLiteral("genre")};
    View table = Schema::makeView(s, ViewType::Table, tr("All books"));
    View board = Schema::makeView(s, ViewType::Board, tr("By status"));
    board.groupBy = QStringLiteral("status");
    s.views = {gallery, table, board};
    return s;
}

Schema journalSchema()
{
    Schema s;
    s.props = {prop(QStringLiteral("title"), tr("Entry"), PropType::Title), prop(QStringLiteral("date"), tr("Date"), PropType::Date),
               prop(QStringLiteral("mood"), tr("Mood"), PropType::Select,
                    {opt(QStringLiteral("bright"), tr("Bright"), 8), opt(QStringLiteral("calm"), tr("Calm"), 5),
                     opt(QStringLiteral("cloudy"), tr("Cloudy"), 2), opt(QStringLiteral("stormy"), tr("Stormy"), 0)}),
               prop(QStringLiteral("tags"), tr("Tags"), PropType::MultiSelect,
                    {opt(QStringLiteral("work"), tr("Work"), 3), opt(QStringLiteral("life"), tr("Life"), 6), opt(QStringLiteral("idea"), tr("Idea"), 9)})};
    View cal = Schema::makeView(s, ViewType::Calendar, tr("Calendar"));
    cal.dateProp = QStringLiteral("date");
    View list = Schema::makeView(s, ViewType::List, tr("Entries"));
    list.sorts = {{QStringLiteral("date"), true}};
    s.views = {cal, list};
    return s;
}

QString isoIn(int days)
{
    return QDate::currentDate().addDays(days).toString(Qt::ISODate);
}
}

QList<Info> all()
{
    return {
        {QStringLiteral("meeting"), tr("Meeting notes"), tr("Agenda, notes, decisions and action items."), QStringLiteral("chat"), false},
        {QStringLiteral("journal"), tr("Daily journal"), tr("Gratitude, focus and a short reflection."), QStringLiteral("moon"), false},
        {QStringLiteral("project"), tr("Project brief"), tr("Goals, scope, milestones and risks on one page."), QStringLiteral("rocket"), false},
        {QStringLiteral("weekly"), tr("Weekly plan"), tr("Seven days, each with its own to-dos."), QStringLiteral("calendar"), false},
        {QStringLiteral("book"), tr("Book notes"), tr("Summary, key ideas and favorite quotes."), QStringLiteral("book"), false},
        {QStringLiteral("wiki"), tr("Personal wiki"), tr("A home page with sections for everything you know."), QStringLiteral("globe"), false},
        {QStringLiteral("tasks"), tr("Task board"), tr("Kanban board with status, priority and due dates."), QStringLiteral("board"), true},
        {QStringLiteral("reading"), tr("Reading list"), tr("A shelf of books with ratings and genres."), QStringLiteral("gallery"), true},
        {QStringLiteral("content"), tr("Content calendar"), tr("Plan what to publish and where."), QStringLiteral("calendar"), true},
        {QStringLiteral("habits"), tr("Habit tracker"), tr("Tick off habits day by day."), QStringLiteral("check-circle"), true},
    };
}

void apply(Workspace &ws, const QString &templateId, const QString &pageId)
{
    const QDate today = QDate::currentDate();
    auto setPage = [&](const QString &title, const QString &icon, const QList<Block> &blocks) {
        if (ws.page(pageId) && ws.page(pageId)->title.trimmed().isEmpty())
            ws.setTitle(pageId, title);
        if (ws.page(pageId) && ws.page(pageId)->icon.isEmpty())
            ws.setIcon(pageId, QStringLiteral("glyph:") + icon);
        ws.setContent(pageId, blocks);
    };
    ws.batch([&] {
        if (templateId == QLatin1String("meeting")) {
            setPage(tr("Meeting notes"), QStringLiteral("chat"),
                    md(QStringLiteral("<aside>\n%1 · %2\n</aside>\n\n## %3\n\n1. \n\n## %4\n\n- \n\n## %5\n\n- [ ] \n\n## %6\n\n> ")
                           .arg(longDate(today), tr("Attendees: "), tr("Agenda"), tr("Notes"), tr("Action items"), tr("Decisions")),
                       {QStringLiteral("people")}));
        } else if (templateId == QLatin1String("journal")) {
            setPage(longDate(today), QStringLiteral("moon"),
                    md(QStringLiteral("### %1\n\n- \n- \n- \n\n### %2\n\n- [ ] \n- [ ] \n\n### %3\n\n<details>\n<summary>%4</summary>\n\n- \n\n</details>\n\n"
                                      "> %5")
                           .arg(tr("Three things I'm grateful for"), tr("Focus for today"), tr("Reflection"), tr("What went well, what to change"),
                                tr("We are made of star-stuff. — Carl Sagan"))));
        } else if (templateId == QLatin1String("project")) {
            setPage(tr("Project brief"), QStringLiteral("rocket"),
                    md(QStringLiteral("<aside>\n%1\n</aside>\n\n## %2\n\n- \n\n## %3\n\n- \n\n## %4\n\n- [ ] %5\n- [ ] %6\n- [ ] %7\n\n"
                                      "## %8\n\n<details>\n<summary>%9</summary>\n\n- \n\n</details>\n\n## %10\n\n- ")
                           .arg(tr("One paragraph: what we're building, for whom, and why now."), tr("Goals"), tr("Non-goals"), tr("Milestones"),
                                tr("Kick-off"), tr("First prototype"), tr("Launch"), tr("Risks"), tr("Known unknowns"), tr("Links")),
                       {QStringLiteral("target")}));
        } else if (templateId == QLatin1String("weekly")) {
            QString text = QStringLiteral("<aside>\n%1\n</aside>\n\n").arg(tr("Week of %1").arg(QLocale(QLocale::English).toString(today, QStringLiteral("MMMM d"))));
            const QDate monday = today.addDays(1 - today.dayOfWeek());
            for (int i = 0; i < 7; ++i)
                text += QStringLiteral("### %1\n\n- [ ] \n\n").arg(QLocale(QLocale::English).toString(monday.addDays(i), QStringLiteral("dddd")));
            setPage(tr("Weekly plan"), QStringLiteral("calendar"), md(text, {QStringLiteral("calendar")}));
        } else if (templateId == QLatin1String("book")) {
            setPage(tr("Book notes"), QStringLiteral("book"),
                    md(QStringLiteral("<aside>\n%1\n</aside>\n\n## %2\n\n\n## %3\n\n1. \n2. \n3. \n\n## %4\n\n> \n\n## %5\n\n")
                           .arg(tr("Author · Year · Where I heard about it"), tr("Summary in three sentences"), tr("Key ideas"), tr("Quotes"),
                                tr("My take")),
                       {QStringLiteral("book")}));
        } else if (templateId == QLatin1String("wiki")) {
            const QString a = ws.createPage(pageId, tr("How I work"));
            ws.setIcon(a, QStringLiteral("glyph:compass"));
            const QString b = ws.createPage(pageId, tr("Tools & setup"));
            ws.setIcon(b, QStringLiteral("glyph:terminal"));
            const QString c = ws.createPage(pageId, tr("Recipes"));
            ws.setIcon(c, QStringLiteral("glyph:coffee"));
            QList<Block> blocks = md(QStringLiteral("<aside>\n%1\n</aside>\n\n## %2\n").arg(tr("Everything I know, in one place."), tr("Sections")),
                                     {QStringLiteral("globe")});
            blocks += {pageLink(a), pageLink(b), pageLink(c)};
            setPage(tr("Personal wiki"), QStringLiteral("globe"), blocks);
        } else if (templateId == QLatin1String("tasks")) {
            if (ws.page(pageId)->title.isEmpty())
                ws.setTitle(pageId, tr("Tasks"));
            ws.setIcon(pageId, QStringLiteral("glyph:board"));
            ws.convertToDatabase(pageId, taskSchema());
            ws.createRow(pageId, tr("Plan the week"), {{QStringLiteral("status"), QStringLiteral("doing")}, {QStringLiteral("priority"), QStringLiteral("high")}, {QStringLiteral("due"), isoIn(1)}});
            ws.createRow(pageId, tr("Reply to emails"), {{QStringLiteral("status"), QStringLiteral("todo")}, {QStringLiteral("priority"), QStringLiteral("med")}});
            ws.createRow(pageId, tr("Water the plants"), {{QStringLiteral("status"), QStringLiteral("done")}, {QStringLiteral("priority"), QStringLiteral("low")}});
        } else if (templateId == QLatin1String("reading")) {
            if (ws.page(pageId)->title.isEmpty())
                ws.setTitle(pageId, tr("Reading list"));
            ws.setIcon(pageId, QStringLiteral("glyph:book"));
            ws.convertToDatabase(pageId, readingSchema());
            const QString r = ws.createRow(pageId, tr("Cosmos"), {{QStringLiteral("author"), QStringLiteral("Carl Sagan")}, {QStringLiteral("status"), QStringLiteral("todo")}});
            ws.setCover(r, QStringLiteral("cosmos:galaxy:7"));
        } else if (templateId == QLatin1String("content")) {
            if (ws.page(pageId)->title.isEmpty())
                ws.setTitle(pageId, tr("Content calendar"));
            ws.setIcon(pageId, QStringLiteral("glyph:calendar"));
            Schema s;
            s.props = {prop(QStringLiteral("title"), tr("Title"), PropType::Title), statusProp(tr("Idea"), tr("Drafting"), tr("Published")),
                       prop(QStringLiteral("publish"), tr("Publish date"), PropType::Date),
                       prop(QStringLiteral("channel"), tr("Channel"), PropType::Select,
                            {opt(QStringLiteral("blog"), tr("Blog"), 6), opt(QStringLiteral("news"), tr("Newsletter"), 3), opt(QStringLiteral("video"), tr("Video"), 8)})};
            View cal = Schema::makeView(s, ViewType::Calendar);
            cal.dateProp = QStringLiteral("publish");
            View board = Schema::makeView(s, ViewType::Board);
            board.groupBy = QStringLiteral("status");
            s.views = {cal, board, Schema::makeView(s, ViewType::Table)};
            ws.convertToDatabase(pageId, s);
            ws.createRow(pageId, tr("Launch announcement"), {{QStringLiteral("status"), QStringLiteral("doing")}, {QStringLiteral("publish"), isoIn(3)}, {QStringLiteral("channel"), QStringLiteral("blog")}});
        } else if (templateId == QLatin1String("habits")) {
            if (ws.page(pageId)->title.isEmpty())
                ws.setTitle(pageId, tr("Habit tracker"));
            ws.setIcon(pageId, QStringLiteral("glyph:check-circle"));
            Schema s;
            s.props = {prop(QStringLiteral("title"), tr("Day"), PropType::Title), prop(QStringLiteral("date"), tr("Date"), PropType::Date),
                       prop(QStringLiteral("read"), tr("Read"), PropType::Checkbox), prop(QStringLiteral("move"), tr("Move"), PropType::Checkbox),
                       prop(QStringLiteral("meditate"), tr("Meditate"), PropType::Checkbox), prop(QStringLiteral("water"), tr("Water"), PropType::Number)};
            View table = Schema::makeView(s, ViewType::Table);
            table.sorts = {{QStringLiteral("date"), true}};
            s.views = {table};
            ws.convertToDatabase(pageId, s);
            for (int i = 0; i < 5; ++i) {
                const QDate d = today.addDays(-i);
                ws.createRow(pageId, QLocale(QLocale::English).toString(d, QStringLiteral("ddd, MMM d")),
                             {{QStringLiteral("date"), d.toString(Qt::ISODate)}, {QStringLiteral("read"), i % 2 == 0}, {QStringLiteral("move"), i != 2},
                              {QStringLiteral("meditate"), i < 3}, {QStringLiteral("water"), 6 + (i % 3)}});
            }
        }
    });
}

QString createSampleWorkspace(Workspace &ws)
{
    QString welcome;
    const QDate today = QDate::currentDate();
    ws.batch([&] {
        welcome = ws.createPage({}, tr("Welcome to Nebula Note"));
        const QString projects = ws.createDatabase({}, tr("Projects"), taskSchema());
        const QString reading = ws.createDatabase({}, tr("Reading List"), readingSchema());
        const QString journal = ws.createDatabase({}, tr("Journal"), journalSchema());
        const QString ideas = ws.createPage({}, tr("Ideas"));

        // ---- welcome ----
        ws.setIcon(welcome, QStringLiteral("glyph:planet"));
        ws.setCover(welcome, QStringLiteral("cosmos:nebula:2024"), 0.5);
        QList<Block> blocks = md(QStringLiteral(
                                     "Nebula Note is a quiet place for notes, documents and databases — everything lives in blocks you can "
                                     "move, nest and turn into one another. This page is a tour; edit it, break it, delete it.\n\n"
                                     "<aside>\nPress **/** anywhere for the command menu, **@** to mention a page, and **Ctrl+P** to jump "
                                     "between pages.\n</aside>\n\n"
                                     "## Write anything\n\n"
                                     "Type Markdown and it turns into blocks as you go: `#` for headings, `-` for bullets, `[]` for to-dos, "
                                     "`>` for toggles, `\"` for quotes and three backticks for code. **Bold**, *italic*, `inline code` and "
                                     "~~strikethrough~~ work inline too.\n\n"
                                     "- Hover a block and grab the ⋮⋮ handle to drag it somewhere else\n"
                                     "- Press Tab to nest a block under the one above\n"
                                    "    - Nested blocks move with their parent\n"
                                     "- Select text for the formatting bar, with gray **shades** instead of colors\n\n"
                                     "- [x] Open Nebula Note\n- [x] Read the welcome page\n- [ ] Create your first page with Ctrl+N\n- [ ] Import your Notion "
                                     "workspace from the sidebar\n\n"
                                     "<details>\n<summary>Toggles hide what you don't need right now</summary>\n\nClick the arrow to fold "
                                     "this away. Anything nested inside goes with it.\n\n</details>\n\n"
                                     "> The nitrogen in our DNA, the calcium in our teeth, the iron in our blood were made in the interiors of "
                                     "collapsing stars. — Carl Sagan\n\n"
                                     "```cpp\n// Code blocks are highlighted in grayscale.\nint main() {\n    return launch(\"Nebula Note\");\n}\n```\n\n"
                                     "---\n\n## Organize with databases\n\nDatabases are pages full of pages. Each one can be seen as a table, "
                                     "a board, a list, a gallery or a calendar, with filters and sorts per view.\n"),
                                 {QStringLiteral("sparkle")});
        blocks += {pageLink(projects), pageLink(reading), pageLink(journal)};
        blocks += md(QStringLiteral("## Arrange side by side\n\nDrag a block to the left or right edge of another — or type "
                                    "**/2 columns** — to put blocks next to each other. Drag the gap between columns to resize them.\n"));
        {
            Block cols;
            cols.type = BlockType::ColumnList;
            cols.widths = {0.5, 0.5};
            cols.columns = {md(QStringLiteral("### Today\n- [ ] Plan the week\n- [ ] Read one chapter\n")),
                            md(QStringLiteral("### Later\n- Start a reading list\n- Try the board view\n"))};
            blocks.append(cols);
        }
        blocks += md(QStringLiteral("Type **/table** for a simple table — Tab moves between cells and Enter goes down a row.\n"));
        {
            Block t;
            t.type = BlockType::Table;
            t.data.insert(QStringLiteral("hr"), true);
            const QList<QStringList> rows = {{tr("Planet"), tr("Moons"), tr("Day length")},
                                             {tr("Mars"), QStringLiteral("2"), tr("24 h 37 min")},
                                             {tr("Jupiter"), QStringLiteral("95"), tr("9 h 56 min")},
                                             {tr("Saturn"), QStringLiteral("146"), tr("10 h 33 min")}};
            for (const QStringList &r : rows) {
                QList<Cell> row;
                for (const QString &cell : r)
                    row.append(Cell{Span{cell}});
                t.rows.append(row);
            }
            t.widths = {180, 120, 180};
            blocks.append(t);
        }
        blocks += md(QStringLiteral("## Link your thinking\n"));
        blocks.append(mention(tr("Mentions show the linked page's live title, like "), ideas,
                              tr(" — and every page lists its backlinks at the bottom.")));
        blocks += md(QStringLiteral("<aside>\nYour notes stay on this computer, in one SQLite file under ~/.local/share/nebula, with a "
                                    "daily backup. Export to Markdown, HTML or PDF from the ••• menu at any time.\n</aside>\n"),
                     {QStringLiteral("shield")});
        ws.setContent(welcome, blocks);

        // ---- projects ----
        ws.setIcon(projects, QStringLiteral("glyph:rocket"));
        ws.setCover(projects, QStringLiteral("cosmos:orbits:88"), 0.5);
        struct P {
            QString title, status, priority;
            int due;
            QStringList tags;
            QString owner, body;
            QString cover;
        };
        const QList<P> ps = {
            {tr("Launch Nebula Note 1.0"), QStringLiteral("doing"), QStringLiteral("high"), 5, {QStringLiteral("eng"), QStringLiteral("design")}, QStringLiteral("Noah"),
             QStringLiteral("## Checklist\n\n- [x] Block editor\n- [x] Databases\n- [ ] Release notes\n- [ ] Screenshots"), QStringLiteral("cosmos:horizon:11")},
            {tr("Design system refresh"), QStringLiteral("done"), QStringLiteral("med"), -12, {QStringLiteral("design")}, QStringLiteral("Ava"),
             QStringLiteral("Shades replace colors: ten pill styles from graphite to starlight."), QStringLiteral("cosmos:ringed:5")},
            {tr("Write the user guide"), QStringLiteral("doing"), QStringLiteral("med"), 9, {QStringLiteral("writing")}, QStringLiteral("Noah"),
             QStringLiteral("Start with the keyboard shortcuts page."), QString()},
            {tr("Import the old Notion workspace"), QStringLiteral("todo"), QStringLiteral("high"), 2, {QStringLiteral("research")}, QString(),
             QStringLiteral("Export as **Markdown & CSV**, then use Import in the sidebar."), QString()},
            {tr("Plan the Q1 roadmap"), QStringLiteral("todo"), QStringLiteral("low"), 21, {QStringLiteral("research"), QStringLiteral("writing")}, QStringLiteral("Mila"),
             QString(), QString()},
            {tr("Telescope companion app"), QStringLiteral("done"), QStringLiteral("low"), -30, {QStringLiteral("eng")}, QStringLiteral("Ava"),
             QStringLiteral("Shipped. Stargazers love the red-light mode."), QStringLiteral("cosmos:deepfield:3")},
        };
        // Tasks span a few days (date ranges) so the timeline and calendar show bars.
        const QList<int> lengths = {6, 10, 8, 3, 12, 14};
        QStringList taskIds;
        for (int i = 0; i < ps.size(); ++i) {
            const P &p = ps[i];
            const int len = lengths.value(i, 4);
            QJsonObject props{{QStringLiteral("status"), p.status}, {QStringLiteral("priority"), p.priority},
                              {QStringLiteral("due"), isoIn(p.due - len) + QLatin1Char('/') + isoIn(p.due)},
                              {QStringLiteral("tags"), QJsonArray::fromStringList(p.tags)}, {QStringLiteral("estimate"), double(len - 1)}};
            if (!p.owner.isEmpty())
                props.insert(QStringLiteral("owner"), p.owner);
            const QString id = ws.createRow(projects, p.title, props, md(p.body));
            if (!p.cover.isEmpty())
                ws.setCover(id, p.cover);
            taskIds.append(id);
        }
        // Sub-items and dependencies: the launch waits for the guide, and has steps of its own.
        ws.enableSubItems(projects, true);
        ws.enableDependencies(projects, true);
        {
            const Schema ps2 = ws.schema(projects);
            if (!ps2.dependencyProp.isEmpty())
                ws.setProp(taskIds[0], ps2.dependencyProp, QJsonArray{taskIds[2]});
            if (!ps2.subItemsProp.isEmpty()) {
                ws.createRow(projects, tr("Release notes"),
                             {{QStringLiteral("status"), QStringLiteral("todo")}, {QStringLiteral("priority"), QStringLiteral("med")},
                              {QStringLiteral("due"), isoIn(4)}, {QStringLiteral("estimate"), 1.0}, {ps2.subItemsProp, QJsonArray{taskIds[0]}}});
                ws.createRow(projects, tr("Final screenshots"),
                             {{QStringLiteral("status"), QStringLiteral("doing")}, {QStringLiteral("priority"), QStringLiteral("high")},
                              {QStringLiteral("due"), isoIn(5)}, {QStringLiteral("estimate"), 2.0}, {ps2.subItemsProp, QJsonArray{taskIds[0]}}});
            }
            // The new relation columns stay out of the board.
            Schema s3 = ws.schema(projects);
            for (View &v : s3.views)
                if (v.type == ViewType::Board || v.type == ViewType::Calendar)
                    for (const Property &p : s3.props)
                        if (p.type == PropType::Relation && !v.hidden.contains(p.id))
                            v.hidden.append(p.id);
            ws.setSchema(projects, s3);
        }

        // ---- reading list ----
        ws.setIcon(reading, QStringLiteral("glyph:book"));
        ws.setCover(reading, QStringLiteral("cosmos:galaxy:42"), 0.5);
        struct R {
            QString title, author, status, rating;
            QStringList genre;
            QString cover;
            int finished;
        };
        const QList<R> rs = {
            {QStringLiteral("Cosmos"), QStringLiteral("Carl Sagan"), QStringLiteral("done"), QStringLiteral("r5"), {QStringLiteral("science")}, QStringLiteral("cosmos:galaxy:9"), -40},
            {QStringLiteral("The Three-Body Problem"), QStringLiteral("Cixin Liu"), QStringLiteral("doing"), QString(), {QStringLiteral("scifi")}, QStringLiteral("cosmos:eclipse:31"), 0},
            {QStringLiteral("Project Hail Mary"), QStringLiteral("Andy Weir"), QStringLiteral("done"), QStringLiteral("r4"), {QStringLiteral("scifi")}, QStringLiteral("cosmos:comet:17"), -12},
            {QStringLiteral("A Brief History of Time"), QStringLiteral("Stephen Hawking"), QStringLiteral("todo"), QString(), {QStringLiteral("science"), QStringLiteral("classic")}, QStringLiteral("cosmos:deepfield:12"), 0},
            {QStringLiteral("The Left Hand of Darkness"), QStringLiteral("Ursula K. Le Guin"), QStringLiteral("todo"), QString(), {QStringLiteral("scifi"), QStringLiteral("classic")}, QStringLiteral("cosmos:aurora:8"), 0},
            {QStringLiteral("Pale Blue Dot"), QStringLiteral("Carl Sagan"), QStringLiteral("done"), QStringLiteral("r5"), {QStringLiteral("essay"), QStringLiteral("science")}, QStringLiteral("cosmos:horizon:64"), -80},
        };
        for (const R &r : rs) {
            QJsonObject props{{QStringLiteral("author"), r.author}, {QStringLiteral("status"), r.status},
                              {QStringLiteral("genre"), QJsonArray::fromStringList(r.genre)}};
            if (!r.rating.isEmpty())
                props.insert(QStringLiteral("rating"), r.rating);
            if (r.finished)
                props.insert(QStringLiteral("finished"), isoIn(r.finished));
            const QString id = ws.createRow(reading, r.title, props);
            ws.setCover(id, r.cover);
        }

        // ---- journal ----
        ws.setIcon(journal, QStringLiteral("glyph:moon"));
        ws.setCover(journal, QStringLiteral("cosmos:lunar:21"), 0.5);
        struct J {
            int day;
            QString title, mood;
            QStringList tags;
            QString body;
        };
        const QList<J> js = {
            {0, tr("Clear skies tonight"), QStringLiteral("bright"), {QStringLiteral("life")}, QStringLiteral("Saturn's rings were visible through the small telescope.")},
            {-1, tr("Long day, good progress"), QStringLiteral("calm"), {QStringLiteral("work")}, QStringLiteral("- [x] Finished the database views\n- [ ] Fix the calendar drag")},
            {-3, tr("Idea: star map wallpaper"), QStringLiteral("bright"), {QStringLiteral("idea")}, QStringLiteral("Generate a wallpaper from tonight's sky over my city.")},
            {-6, tr("Rain all weekend"), QStringLiteral("cloudy"), {QStringLiteral("life")}, QString()},
            {-9, tr("Planning session"), QStringLiteral("calm"), {QStringLiteral("work")}, QString()},
            {2, tr("Meteor shower peak"), QString(), {QStringLiteral("life")}, QStringLiteral("Set an alarm for 3 a.m.")},
        };
        for (const J &j : js) {
            QJsonObject props{{QStringLiteral("date"), isoIn(j.day)}, {QStringLiteral("tags"), QJsonArray::fromStringList(j.tags)}};
            if (!j.mood.isEmpty())
                props.insert(QStringLiteral("mood"), j.mood);
            ws.createRow(journal, j.title, props, md(j.body));
        }

        // ---- a gallery of blocks ----
        {
            const QString gallery = ws.createPage(welcome, tr("Block gallery"));
            ws.setIcon(gallery, QStringLiteral("glyph:constellation"));
            QList<Block> g;
            Block crumb;
            crumb.type = BlockType::Breadcrumb;
            g.append(crumb);
            g += md(tr("Every kind of block Nebula Note has, in one place. Drag any of them around — or press **Esc** to select a "
                       "block and use the arrow keys.\n"));
            Block toc;
            toc.type = BlockType::TableOfContents;
            g.append(toc);
            g += md(QStringLiteral("## %1\n").arg(tr("Math")));
            Block eq;
            eq.type = BlockType::Equation;
            eq.data.insert(QStringLiteral("expr"), QStringLiteral("\\int_0^{\\infty} e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}"));
            eq.caption = {Span{tr("The Gaussian integral")}};
            g.append(eq);
            {
                Block line = Block::make(BlockType::Paragraph, tr("Inline math sits in a sentence, like "));
                Span m;
                m.text = QString(QChar::ObjectReplacementCharacter);
                m.equation = QStringLiteral("a^2 + b^2 = c^2");
                line.spans.append(m);
                line.spans.append(Span{tr(" — type $$ around TeX, or press Ctrl+Shift+E.")});
                g.append(line);
            }
            g += md(QStringLiteral("## %1\n").arg(tr("Dates and reminders")));
            {
                Block line = Block::make(BlockType::Todo, tr("Watch the meteor shower "));
                Span d;
                d.text = QString(QChar::ObjectReplacementCharacter);
                d.date = isoIn(2).left(10) + QStringLiteral("T22:00");
                d.remind = true;
                line.spans.append(d);
                g.append(line);
            }
            g += md(QStringLiteral("## %1\n").arg(tr("Toggle headings")));
            Block th = Block::make(BlockType::Heading3, tr("Click the arrow to fold this section"));
            th.toggleable = true;
            g.append(th);
            g.append(Block::make(BlockType::Paragraph, tr("Everything indented under a toggle heading folds with it."), 1));
            g += md(QStringLiteral("## %1\n").arg(tr("Buttons and links")));
            Block button;
            button.type = BlockType::Button;
            button.data.insert(QStringLiteral("label"), tr("Add a stargazing log"));
            button.data.insert(QStringLiteral("actions"),
                               QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("insert")},
                                                      {QStringLiteral("markdown"), QStringLiteral("- [ ] %1").arg(tr("Note what you saw"))}}});
            g.append(button);
            Block bookmark;
            bookmark.type = BlockType::Bookmark;
            bookmark.url = QStringLiteral("https://science.nasa.gov/solar-system/");
            bookmark.data.insert(QStringLiteral("title"), tr("Our Solar System"));
            bookmark.data.insert(QStringLiteral("desc"), tr("Explore the planets, moons and small bodies of our cosmic neighborhood."));
            g.append(bookmark);
            g += md(QStringLiteral("## %1\n").arg(tr("Diagrams")));
            Block mermaid = Block::make(BlockType::Code, QStringLiteral("graph LR") + QChar::LineSeparator + QStringLiteral("    A[Dust cloud] --> B(Protostar)")
                                                            + QChar::LineSeparator + QStringLiteral("    B --> C{Mass?}") + QChar::LineSeparator
                                                            + QStringLiteral("    C -->|small| D([Red dwarf])") + QChar::LineSeparator
                                                            + QStringLiteral("    C -->|large| E((Supernova))"));
            mermaid.language = QStringLiteral("mermaid");
            g.append(mermaid);
            g += md(QStringLiteral("## %1\n").arg(tr("Callouts hold blocks")));
            Block callout = Block::make(BlockType::Callout, tr("Press Enter at the end of a callout to keep writing inside it."));
            callout.icon = QStringLiteral("glyph:sparkle");
            g.append(callout);
            g.append(Block::make(BlockType::Bullet, tr("Lists, toggles and more fit inside"), 1));
            g.append(Block::make(BlockType::Bullet, tr("Drag blocks in and out"), 1));
            // An inline database lives in the page; a linked view shows another database.
            g += md(QStringLiteral("## %1\n").arg(tr("Inline databases")));
            {
                Schema obs;
                obs.props = {prop(QStringLiteral("title"), tr("Object"), PropType::Title),
                             prop(QStringLiteral("type"), tr("Type"), PropType::Select,
                                  {opt(QStringLiteral("planet"), tr("Planet"), 8), opt(QStringLiteral("nebula"), tr("Nebula"), 5),
                                   opt(QStringLiteral("galaxy"), tr("Galaxy"), 2)}),
                             prop(QStringLiteral("mag"), tr("Magnitude"), PropType::Number),
                             prop(QStringLiteral("seen"), tr("Seen"), PropType::Checkbox)};
                View t = Schema::makeView(obs, ViewType::Table, tr("Observations"));
                t.calcs = {{QStringLiteral("mag"), QStringLiteral("avg")}, {QStringLiteral("seen"), QStringLiteral("checked")}};
                View b = Schema::makeView(obs, ViewType::Board, tr("By type"));
                b.groupBy = QStringLiteral("type");
                obs.views = {t, b};
                const QString obsDb = ws.createDatabase(gallery, tr("Sky log"), obs);
                ws.createRow(obsDb, tr("Jupiter"), {{QStringLiteral("type"), QStringLiteral("planet")}, {QStringLiteral("mag"), -2.7}, {QStringLiteral("seen"), true}});
                ws.createRow(obsDb, tr("Orion Nebula"), {{QStringLiteral("type"), QStringLiteral("nebula")}, {QStringLiteral("mag"), 4.0}, {QStringLiteral("seen"), true}});
                ws.createRow(obsDb, tr("Andromeda"), {{QStringLiteral("type"), QStringLiteral("galaxy")}, {QStringLiteral("mag"), 3.4}});
                Block inlineDb;
                inlineDb.type = BlockType::Database;
                inlineDb.pageId = obsDb;
                g.append(inlineDb);
            }
            g += md(tr("A linked view shows **Projects** here with its own filters — only what's in progress:\n"));
            {
                Schema ps = ws.schema(projects);
                View linked = Schema::makeView(ps, ViewType::List, tr("In progress"));
                linked.filters = {{QStringLiteral("status"), QStringLiteral("is"), QStringLiteral("doing")}};
                Schema holder;
                holder.views = {linked};
                Block lv;
                lv.type = BlockType::Database;
                lv.pageId = projects;
                lv.data.insert(QStringLiteral("linked"), true);
                lv.data.insert(QStringLiteral("views"), holder.toJson().value(QStringLiteral("views")));
                g.append(lv);
            }
            ws.setContent(gallery, g);
        }

        // ---- ideas ----
        ws.setIcon(ideas, QStringLiteral("glyph:bulb"));
        const QString garden = ws.createPage(ideas, tr("A garden on Mars"),
                                             md(QStringLiteral("Regolith needs perchlorates washed out before anything grows.\n\n- Potatoes, obviously\n- Lettuce under LED arrays")));
        ws.setIcon(garden, QStringLiteral("glyph:plant"));
        const QString wallpaper = ws.createPage(ideas, tr("Star map wallpaper"),
                                                md(QStringLiteral("Render the sky for a place and time, in grayscale, at 5K.")));
        ws.setIcon(wallpaper, QStringLiteral("glyph:constellation"));
        QList<Block> ideaBlocks = md(QStringLiteral("Half-formed thoughts worth keeping.\n"));
        ideaBlocks += {pageLink(garden), pageLink(wallpaper)};
        ws.setContent(ideas, ideaBlocks);

        ws.setFavorite(welcome, true);
        ws.setFavorite(projects, true);
        // Seed "recently visited" for the Home page.
        const qint64 now = Ids::now();
        int minutesAgo = 50;
        for (const QString &id : {ideas, journal, reading, projects, welcome})
            ws.markVisited(id, now - (minutesAgo -= 10) * 60000);
        Q_UNUSED(today)
    });
    return welcome;
}

}
