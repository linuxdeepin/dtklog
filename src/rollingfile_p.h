// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef DTKLOG_ROLLINGFILE_P_H
#define DTKLOG_ROLLINGFILE_P_H
#include "RollingFileAppender.h"
#include <QTimeZone>
DLOG_CORE_BEGIN_NAMESPACE
namespace detail {
inline QDateTime nextRollover(RollingFileAppender::DatePattern pattern,
                       const QDateTime &from)
{
    QDate date = from.date();
    QTime time(0, 0);

    switch (pattern) {
    case RollingFileAppender::MinutelyRollover:
        // Advance elapsed time without reconstructing an ambiguous wall time.
        // setTime() can select the other occurrence during an autumn DST fold.
        return from.addMSecs(60000 - from.time().second() * 1000
                             - from.time().msec());
    case RollingFileAppender::HourlyRollover: {
        QDateTime next = from;
        do {
            next = next.addMSecs((60 - next.time().minute()) * 60000
                                  - next.time().second() * 1000
                                  - next.time().msec());
        } while (next.isValid() && next.time().minute() != 0);
        // A thirty-minute offset change can land between whole-hour boundaries.
        return next;
    }
    case RollingFileAppender::HalfDailyRollover: {
        if (from.time().hour() < 12)
            time = QTime(12, 0);
        else
            date = date.addDays(1);
        break;
    }
    case RollingFileAppender::DailyRollover:
        date = date.addDays(1);
        break;
    case RollingFileAppender::WeeklyRollover: {
        date = date.addDays(1);
        while (date.dayOfWeek() != 1)
            date = date.addDays(1);
        break;
    }
    case RollingFileAppender::MonthlyRollover:
        date = QDate(date.year(), date.month(), 1).addMonths(1);
        break;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // Some zones skip midnight. Choose the first valid instant of that date
    // rather than leaving the rollover disabled by an invalid QDateTime.
    if (time == QTime(0, 0))
        return date.startOfDay(from.timeZone());
#endif
    QDateTime next = from;
    next.setDate(date);
    next.setTime(time);
    return next;
}

} // namespace detail
DLOG_CORE_END_NAMESPACE
#endif
