#pragma once

#include <QString>

namespace Ids
{
// A page id: 32 lowercase hex digits (a UUID without dashes, like Notion).
QString newPageId();
// A short random id for properties, options and views.
QString newShortId(int length = 6);
// Milliseconds since the epoch; overridable in tests.
qint64 now();
void setClockForTests(qint64 msecs); // 0 restores the real clock
}
