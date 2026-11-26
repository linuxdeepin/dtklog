// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "JournalAppender.h"
#include <cstdio>
#include <cstring>

#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>

DLOG_CORE_BEGIN_NAMESPACE

void JournalAppender::append(const QDateTime &time,
                             Logger::LogLevel level,
                             const char *file, int line,
                             const char *func,
                             const QString &category,
                             const QString &msg)
{
    int priority = LOG_INFO;
    switch (level) {
    case Logger::Trace:
    case Logger::Debug:
        priority = LOG_DEBUG;
        break;
    case Logger::Info:
        priority = LOG_INFO;
        break;
    case Logger::Warning:
        priority = LOG_WARNING;
        break;
    case Logger::Error:
        priority = LOG_ERR;
        break;
    case Logger::Fatal:
        priority = LOG_CRIT;
        break;
    }

    const QByteArray message = msg.toUtf8();
    const QByteArray sourceFile = QByteArray(file ? file : "");
    const QByteArray sourceFunction = QByteArray(func ? func : "");
    const QByteArray sourceCategory = category.toUtf8();
    const qint64 timestamp = time.toMSecsSinceEpoch() * 1000;

    const int result = sd_journal_send("MESSAGE=%s", message.constData(),
                    "PRIORITY=%d", priority,
                    "DTK_LOG_LEVEL=%d", int(level),
                    "DTKPRIORITTY=%d", int(level),
                    "DTK_TIMESTAMP_USEC=%lld", static_cast<long long>(timestamp),
                    "CODE_FILE=%s", sourceFile.constData(),
                    "CODE_LINE=%d", line,
                    "CODE_FUNC=%s", sourceFunction.constData(),
                    "CODE_CATEGORY=%s", sourceCategory.constData(),
                    nullptr);
    if (result < 0)
        std::fprintf(stderr, "dtklog: cannot send journal log: %s\n", std::strerror(-result));
}

DLOG_CORE_END_NAMESPACE
