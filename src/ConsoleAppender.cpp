// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "ConsoleAppender.h"

#include <QByteArray>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <mutex>
#ifdef Q_OS_WIN
#include <io.h>
#define DLOG_ISATTY _isatty
#define DLOG_FILENO _fileno
#else
#include <unistd.h>
#define DLOG_ISATTY isatty
#define DLOG_FILENO fileno
#endif

namespace {
std::mutex environmentPatternMutex;
}

DLOG_CORE_BEGIN_NAMESPACE

ConsoleAppender::ConsoleAppender()
    : AbstractStringAppender(),
      m_ignoreEnvPattern(false)
{
}

QString ConsoleAppender::format() const
{
    std::lock_guard<std::mutex> lock(environmentPatternMutex);
    const QString environmentPattern =
        QString::fromLocal8Bit(qgetenv("QT_MESSAGE_PATTERN"));
    if (m_ignoreEnvPattern || environmentPattern.isEmpty())
        return AbstractStringAppender::format();
    return environmentPattern + QLatin1Char('\n');
}

void ConsoleAppender::ignoreEnvironmentPattern(bool ignore)
{
    std::lock_guard<std::mutex> lock(environmentPatternMutex);
    m_ignoreEnvPattern = ignore;
}

void ConsoleAppender::append(const QDateTime &time, Logger::LogLevel level,
                             const char *file, int line, const char *func,
                             const QString &category, const QString &msg)
{
    const QString text = formattedString(time, level, file, line, func,
                                         category, msg, DLOG_ISATTY(DLOG_FILENO(stdout)));
    QByteArray bytes = text.toLocal8Bit();
    // Custom formats need the record terminator previously supplied by the sink.
    if (!bytes.endsWith('\n'))
        bytes.append('\n');
    // A previous failure must not permanently poison a recovered destination.
    clearerr(stdout);
    const size_t written = fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    const int flushed = fflush(stdout);
    if (written != size_t(bytes.size()) || flushed != 0)
        std::fprintf(stderr, "dtklog: cannot write console log: %s\n", std::strerror(errno));
}

DLOG_CORE_END_NAMESPACE
