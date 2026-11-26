// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "AbstractAppender.h"

#include <QMutexLocker>
#include <algorithm>
#include <vector>

DLOG_CORE_BEGIN_NAMESPACE

AbstractAppender::AbstractAppender()
    : m_detailsLevel(Logger::Debug)
{
}

AbstractAppender::~AbstractAppender() = default;

Logger::LogLevel AbstractAppender::detailsLevel() const
{
    QMutexLocker locker(&m_detailsLevelMutex);
    return m_detailsLevel;
}

void AbstractAppender::setDetailsLevel(Logger::LogLevel level)
{
    QMutexLocker locker(&m_detailsLevelMutex);
    m_detailsLevel = level;
}

void AbstractAppender::setDetailsLevel(const QString &level)
{
    setDetailsLevel(Logger::levelFromString(level));
}

void AbstractAppender::write(const QDateTime &time, Logger::LogLevel level,
                             const char *file, int line, const char *func,
                             const QString &category, const QString &msg)
{
    if (level < detailsLevel())
        return;

    // A custom appender can itself log (e.g. on an I/O error). Avoid taking
    // the same non-recursive ABI-frozen mutex again on this thread.
    static thread_local std::vector<const AbstractAppender *> active;
    if (std::find(active.begin(), active.end(), this) != active.end())
        return;
    struct ActiveRecord {
        std::vector<const AbstractAppender *> &records;
        ActiveRecord(std::vector<const AbstractAppender *> &r, const AbstractAppender *a)
            : records(r) { records.push_back(a); }
        ~ActiveRecord() { records.pop_back(); }
    } record(active, this);
    QMutexLocker locker(&m_writeMutex);
    append(time, level, file, line, func, category, msg);
}

DLOG_CORE_END_NAMESPACE
