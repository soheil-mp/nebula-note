#include "Ids.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QUuid>

namespace
{
qint64 g_fakeClock = 0;
}

namespace Ids
{

QString newPageId()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

QString newShortId(int length)
{
    static const char alphabet[] = "abcdefghijkmnopqrstuvwxyz23456789";
    QString s;
    s.reserve(length);
    for (int i = 0; i < length; ++i)
        s.append(QLatin1Char(alphabet[QRandomGenerator::global()->bounded(int(sizeof(alphabet) - 1))]));
    return s;
}

qint64 now()
{
    if (g_fakeClock)
        return g_fakeClock;
    return QDateTime::currentMSecsSinceEpoch();
}

void setClockForTests(qint64 msecs)
{
    g_fakeClock = msecs;
}

}
