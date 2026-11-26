// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifdef _WIN32

#include "OutputDebugAppender.h"

#include <windows.h>

DLOG_CORE_BEGIN_NAMESPACE

void OutputDebugAppender::append(const QDateTime &time,
                                 Logger::LogLevel level,
                                 const char *file, int line,
                                 const char *func,
                                 const QString &category,
                                 const QString &msg)
{
    QString text = formattedString(time, level, file, line, func, category, msg);
    if (!text.endsWith(QLatin1Char('\n')))
        text.append(QLatin1Char('\n'));
    const std::wstring wideText = text.toStdWString();
    OutputDebugStringW(wideText.c_str());
}

DLOG_CORE_END_NAMESPACE

#endif
