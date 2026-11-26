// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef DTKLOG_LOGSTREAM_P_H
#define DTKLOG_LOGSTREAM_P_H

#include "dlog_global.h"
#include <QDebug>
#include <private/qtextstream_p.h>
#include <functional>
#include <memory>

DLOG_CORE_BEGIN_NAMESPACE
namespace detail {

// Logger::write() must keep returning QDebug for existing binaries. Qt's public
// QDebug API has no finalization callback. Use the already-required CorePrivate
// dependency to observe destruction of its QTextStream, independent of message
// handlers and Qt category filters. Explicit member-pointer instantiation keeps
// this bridge checked against the Qt headers, without assuming byte offsets or
// replacing any Qt class. Keep all Qt-private access in this one header.
Q_DECL_HIDDEN QTextStream &debugTextStream(QDebug &debug);
Q_DECL_HIDDEN std::function<bool()> debugSpacing(QDebug &debug);
Q_DECL_HIDDEN QTextStreamPrivate &textStreamPrivate(QTextStream &stream);

template <typename Member>
struct SpacingReader
{
    Member stream;
    bool operator()() const { return stream->space; }
};

template <typename Member, Member QDebug::*member>
struct DebugStreamAccess
{
    friend QTextStream &debugTextStream(QDebug &debug)
    { return (debug.*member)->ts; }
    friend std::function<bool()> debugSpacing(QDebug &debug)
    {
        return SpacingReader<Member>{debug.*member};
    }
};
template struct DebugStreamAccess<decltype(QDebug::stream), &QDebug::stream>;

template <typename Member, Member QTextStream::*member>
struct TextStreamAccess
{
    friend QTextStreamPrivate &textStreamPrivate(QTextStream &stream)
    { return *(stream.*member); }
};
template struct TextStreamAccess<decltype(QTextStream::d_ptr), &QTextStream::d_ptr>;

inline QDebug completedStream(std::function<void(const QString &)> complete)
{
    // The notifier is destroyed with the QTextStream, including empty records
    // and the final copy of a QDebug. Keep its string alive until that point.
    const auto buffer = std::make_shared<QString>();
    QDebug debug(buffer.get());
    const auto spacing = debugSpacing(debug);
    QObject::connect(&textStreamPrivate(debugTextStream(debug)).deviceClosedNotifier,
                     &QObject::destroyed,
                     [buffer, complete, spacing]() {
        if (spacing() && buffer->endsWith(QLatin1Char(' ')))
            buffer->chop(1);
        complete(*buffer);
    });
    return debug;
}

} // namespace detail
DLOG_CORE_END_NAMESPACE
#endif
