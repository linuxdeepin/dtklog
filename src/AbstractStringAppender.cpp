// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "AbstractStringAppender.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QReadLocker>
#include <QThread>
#include <QVector>
#include <QWriteLocker>

namespace {

QString levelColor(Dtk::Core::Logger::LogLevel level, const QString &text)
{
    const char *color = "\x1b[0m";
    switch (level) {
    case Dtk::Core::Logger::Trace:
        color = "\x1b[94m";
        break;
    case Dtk::Core::Logger::Debug:
        color = "\x1b[36m";
        break;
    case Dtk::Core::Logger::Info:
        color = "\x1b[32m";
        break;
    case Dtk::Core::Logger::Warning:
        color = "\x1b[33m";
        break;
    case Dtk::Core::Logger::Error:
        color = "\x1b[31m";
        break;
    case Dtk::Core::Logger::Fatal:
        color = "\x1b[35m";
        break;
    }
    return QString::fromLatin1(color) + text + QStringLiteral("\x1b[0m");
}

QByteArray removeTemplateSuffix(QByteArray info)
{
    const int marker = info.indexOf(" [with ");
    if (marker >= 0)
        info.truncate(marker);
    return info;
}

QByteArray removeTrailingQualifiers(QByteArray info)
{
    static const char *const qualifiers[] {
        " const", " volatile", " noexcept", " &", " &&", " override", " final"
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (const char *qualifier : qualifiers) {
            const QByteArray suffix(qualifier);
            if (info.endsWith(suffix)) {
                info.chop(suffix.size());
                changed = true;
            }
        }
    }
    return info.trimmed();
}

int matchingArgumentListStart(const QByteArray &info)
{
    if (!info.endsWith(')'))
        return -1;

    int depth = 0;
    for (int index = info.size() - 1; index >= 0; --index) {
        if (info.at(index) == ')')
            ++depth;
        else if (info.at(index) == '(' && --depth == 0)
            return index;
    }
    return -1;
}

QByteArray functionNameWithoutReturnType(QByteArray info)
{
    // Operator punctuation is part of the name, not a template delimiter.
    const int operatorAt = info.lastIndexOf("operator");
    const int scanEnd = operatorAt >= 0 ? operatorAt : info.size();
    int depth = 0;
    int nameStart = 0;
    for (int i = scanEnd - 1; i >= 0; --i) {
        if (info.at(i) == '>')
            ++depth;
        else if (info.at(i) == '<')
            --depth;
        else if (info.at(i) == ' ' && depth == 0) {
            nameStart = i + 1;
            break;
        }
    }
    info = info.mid(nameStart);
    while (info.startsWith('*') || info.startsWith('&'))
        info.remove(0, 1);

    QByteArray result;
    depth = 0;
    const int operatorStart = info.indexOf("operator");
    for (int i = 0; i < info.size(); ++i) {
        if (operatorStart >= 0 && i >= operatorStart) {
            result.append(info.mid(i));
            break;
        }
        const char c = info.at(i);
        if (c == '<')
            ++depth;
        else if (c == '>' && depth > 0)
            --depth;
        else if (depth == 0)
            result.append(c);
    }
    return result;
}

bool markerMatchesLevel(Dtk::Core::Logger::LogLevel level, const QString &condition)
{
    if (condition == QLatin1String("if-debug"))
        return level == Dtk::Core::Logger::Debug;
    if (condition == QLatin1String("if-info"))
        return level == Dtk::Core::Logger::Info;
    if (condition == QLatin1String("if-warning"))
        return level == Dtk::Core::Logger::Warning;
    if (condition == QLatin1String("if-critical")
        || condition == QLatin1String("if-error"))
        return level == Dtk::Core::Logger::Error;
    if (condition == QLatin1String("if-fatal"))
        return level == Dtk::Core::Logger::Fatal;
    return false;
}

} // namespace

DLOG_CORE_BEGIN_NAMESPACE

AbstractStringAppender::AbstractStringAppender()
    : m_format(QStringLiteral(
          "%{time}{yyyy-MM-ddTHH:mm:ss.zzz} [%{type:-7}] <%{function}> %{message}\n"))
{
}

QString AbstractStringAppender::format() const
{
    QReadLocker locker(&m_formatLock);
    return m_format;
}

void AbstractStringAppender::setFormat(const QString &format)
{
    QWriteLocker locker(&m_formatLock);
    m_format = format;
}

QString AbstractStringAppender::stripFunctionName(const char *name)
{
    return QString::fromLatin1(qCleanupFuncinfo(name));
}

QByteArray AbstractStringAppender::qCleanupFuncinfo(const char *name)
{
    if (!name)
        return QByteArray();

    QByteArray info = removeTrailingQualifiers(
        removeTemplateSuffix(QByteArray(name).trimmed()));
    if (info.isEmpty())
        return info;

    const int lambdaMarker = info.indexOf("::<lambda");
    const bool lambda = lambdaMarker >= 0;
    if (lambda)
        info.truncate(lambdaMarker);
    const int argumentStart = matchingArgumentListStart(info);
    if (argumentStart > 0)
        info.truncate(argumentStart);

    info.replace("operator ", "operator");
    info = functionNameWithoutReturnType(info.trimmed());

    if (lambda)
        info.append("::lambda");
    return info;
}

QString AbstractStringAppender::formattedString(const QDateTime &time,
                                                Logger::LogLevel level,
                                                const char *file, int line,
                                                const char *func,
                                                const QString &category,
                                                const QString &msg) const
{
    return formattedString(time, level, file, line, func, category, msg, false);
}

QString AbstractStringAppender::formattedString(const QDateTime &time,
                                                Logger::LogLevel level,
                                                const char *file, int line,
                                                const char *func,
                                                const QString &category,
                                                const QString &msg,
                                                bool withColor) const
{
    const QString pattern = format();
    const QString fullFile = QString::fromLocal8Bit(file ? file : "");
    const QString fullFunction = QString::fromLocal8Bit(func ? func : "");
    const QString safeCategory = category.isNull() ? QString() : category;
    QString output;
    QVector<bool> conditions;

    for (int index = 0; index < pattern.size();) {
        if (pattern.at(index) != QLatin1Char('%')) {
            if (conditions.isEmpty() || conditions.last())
                output.append(pattern.at(index));
            ++index;
            continue;
        }

        if (index + 1 < pattern.size() && pattern.at(index + 1) == QLatin1Char('%')) {
            if (conditions.isEmpty() || conditions.last())
                output.append(QLatin1Char('%'));
            index += 2;
            continue;
        }

        QString command;
        int width = 0;
        QString timeFormat;
        int next = index + 1;

        if (next < pattern.size() && pattern.at(next) == QLatin1Char('{')) {
            const int commandEnd = pattern.indexOf(QLatin1Char('}'), next + 1);
            if (commandEnd < 0) {
                if (conditions.isEmpty() || conditions.last())
                    output.append(QLatin1Char('%'));
                ++index;
                continue;
            }

            const QString specification =
                pattern.mid(next + 1, commandEnd - next - 1);
            const int widthMarker = specification.lastIndexOf(QLatin1Char(':'));
            if (specification.startsWith(QLatin1String("time "))) {
                command = QStringLiteral("time");
                timeFormat = specification.mid(5);
            } else if (widthMarker > 0) {
                command = specification.left(widthMarker);
                bool ok = false;
                width = specification.mid(widthMarker + 1).toInt(&ok);
                if (!ok)
                    width = 0;
            } else {
                command = specification;
            }

            next = commandEnd + 1;
            if (command == QLatin1String("time") && timeFormat.isEmpty()
                && next < pattern.size() && pattern.at(next) == QLatin1Char('{')) {
                const int argumentEnd =
                    pattern.indexOf(QLatin1Char('}'), next + 1);
                if (argumentEnd >= 0) {
                    timeFormat =
                        pattern.mid(next + 1, argumentEnd - next - 1);
                    next = argumentEnd + 1;
                }
            }
        } else {
            int commandEnd = next;
            while (commandEnd < pattern.size()
                   && pattern.at(commandEnd).isLetter()) {
                ++commandEnd;
            }
            if (commandEnd == next) {
                if (conditions.isEmpty() || conditions.last())
                    output.append(QLatin1Char('%'));
                ++index;
                continue;
            }
            command = pattern.mid(next, commandEnd - next);
            next = commandEnd;
        }

        const QString normalizedCommand = command.trimmed();
        const bool active = conditions.isEmpty() || conditions.last();

        if (normalizedCommand == QLatin1String("if-category")) {
            conditions.append(active && !safeCategory.isEmpty());
        } else if (normalizedCommand.startsWith(QLatin1String("if-"))) {
            conditions.append(active
                              && markerMatchesLevel(level, normalizedCommand));
        } else if (normalizedCommand == QLatin1String("endif")) {
            if (!conditions.isEmpty())
                conditions.removeLast();
        } else if (active) {
            QString value;
            if (normalizedCommand == QLatin1String("time")) {
                value = time.toString(
                    timeFormat.isEmpty()
                        ? QStringLiteral("HH:mm:ss.zzz")
                        : timeFormat);
            } else if (normalizedCommand == QLatin1String("type")
                       || normalizedCommand == QLatin1String("level")) {
                value = Logger::levelToString(level);
                if (withColor)
                    value = levelColor(level, value);
            } else if (normalizedCommand == QLatin1String("Type")) {
                value = Logger::levelToString(level).toUpper();
                if (withColor)
                    value = levelColor(level, value);
            } else if (normalizedCommand == QLatin1String("typeOne")) {
                value = Logger::levelToString(level).left(1).toLower();
                if (withColor)
                    value = levelColor(level, value);
            } else if (normalizedCommand == QLatin1String("TypeOne")) {
                value = Logger::levelToString(level).left(1).toUpper();
                if (withColor)
                    value = levelColor(level, value);
            } else if (normalizedCommand == QLatin1String("File")) {
                value = fullFile;
            } else if (normalizedCommand == QLatin1String("file")) {
                value = QFileInfo(fullFile).fileName();
            } else if (normalizedCommand == QLatin1String("line")) {
                value = QString::number(line);
            } else if (normalizedCommand == QLatin1String("Function")) {
                value = fullFunction;
            } else if (normalizedCommand == QLatin1String("function")) {
                value = stripFunctionName(func);
            } else if (normalizedCommand == QLatin1String("message")) {
                value = msg;
            } else if (normalizedCommand == QLatin1String("category")) {
                value = safeCategory;
            } else if (normalizedCommand == QLatin1String("pid")) {
                value = QString::number(QCoreApplication::applicationPid());
            } else if (normalizedCommand == QLatin1String("appname")) {
                value = QCoreApplication::applicationName();
            } else if (normalizedCommand == QLatin1String("threadid")) {
                value = QLatin1String("0x")
                    + QString::number(
                        qulonglong(QThread::currentThreadId()), 16);
            } else {
                value = pattern.mid(index, next - index);
            }

            // Keep malformed widths bounded, including INT_MIN.
            width = qBound(-65536, width, 65536);
            const bool sourceField = normalizedCommand == QLatin1String("file")
                || normalizedCommand == QLatin1String("File")
                || normalizedCommand == QLatin1String("function")
                || normalizedCommand == QLatin1String("Function");
            const bool missingLocation = normalizedCommand == QLatin1String("line")
                && line == 0 && fullFile.isEmpty() && fullFunction.isEmpty()
                && (pattern.contains(QLatin1String("%{file"))
                    || pattern.contains(QLatin1String("%{File"))
                    || pattern.contains(QLatin1String("%{function"))
                    || pattern.contains(QLatin1String("%{Function")));
            if ((sourceField && value.isEmpty()) || missingLocation) {
                index = next;
                continue;
            }
            output.append(QStringLiteral("%1").arg(value, width));
        }
        index = next;
    }

    return output;
}

DLOG_CORE_END_NAMESPACE
