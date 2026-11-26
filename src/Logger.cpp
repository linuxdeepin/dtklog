// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "Logger.h"
#include "AbstractAppender.h"
#include "AbstractStringAppender.h"
#include "logstream_p.h"

#include <QCoreApplication>
#include <QReadWriteLock>
#include <QHash>
#include <QGlobalStatic>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <iostream>

DLOG_CORE_BEGIN_NAMESPACE

namespace {
struct AppenderSlot
{
    explicit AppenderSlot(AbstractAppender *value) : appender(value) {}
    std::recursive_mutex mutex;
    AbstractAppender *appender;
};
using Slot = std::shared_ptr<AppenderSlot>;

struct LoggerState
{
    std::mutex mutex;
    bool alive = true;
    bool global = false;
    QList<Slot> appenders;
    QMultiMap<QString, Slot> categoryAppenders;
    QHash<AbstractAppender *, Slot> registrations;
    QString defaultCategory;
};
} // namespace

class LoggerPrivate
{
public:
    std::shared_ptr<LoggerState> state = std::make_shared<LoggerState>();
    static Logger *globalInstance;
    static std::shared_ptr<LoggerState> globalState;
    static QReadWriteLock globalInstanceLock;
};

Logger *LoggerPrivate::globalInstance = nullptr;
std::shared_ptr<LoggerState> LoggerPrivate::globalState;
QReadWriteLock LoggerPrivate::globalInstanceLock;

namespace {
struct HandlerState
{
    std::mutex mutex;
    QtMessageHandler previousHandler = nullptr;
};
Q_GLOBAL_STATIC(HandlerState, handlerState)

void qtMessageHandler(QtMsgType, const QMessageLogContext &, const QString &);

void installMessageHandler()
{
    std::lock_guard<std::mutex> lock(handlerState()->mutex);
    const QtMessageHandler previous = qInstallMessageHandler(qtMessageHandler);
    if (previous != qtMessageHandler)
        handlerState()->previousHandler = previous;
}

void cleanupLoggerGlobalInstance()
{
    Logger *logger;
    {
        QWriteLocker locker(&LoggerPrivate::globalInstanceLock);
        logger = LoggerPrivate::globalInstance;
        LoggerPrivate::globalInstance = nullptr;
        LoggerPrivate::globalState.reset();
    }
    if (handlerState.exists()) {
        std::lock_guard<std::mutex> lock(handlerState()->mutex);
        const QtMessageHandler current = qInstallMessageHandler(handlerState()->previousHandler);
        if (current != qtMessageHandler)
            qInstallMessageHandler(current);
    }
    delete logger;
}

std::shared_ptr<LoggerState> globalState()
{
    Logger::globalInstance();
    QReadLocker locker(&LoggerPrivate::globalInstanceLock);
    return LoggerPrivate::globalState;
}

Logger::LogLevel qtLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return Logger::Debug;
    case QtInfoMsg: return Logger::Info;
    case QtWarningMsg: return Logger::Warning;
    case QtCriticalMsg: return Logger::Error;
    case QtFatalMsg: return Logger::Fatal;
    }
    return Logger::Warning;
}

void dispatch(const std::shared_ptr<LoggerState> &state, const QDateTime &time,
              Logger::LogLevel level, const char *file, int line,
              const char *func, const char *category, const QString &message,
              bool forwarded = false)
{
    if (!state)
        return;
    QString logCategory = QString::fromLatin1(category);
    QList<Slot> selected;
    bool categorySelected = false;
    {
        std::lock_guard<std::mutex> locker(state->mutex);
        if (!state->alive)
            return;
        if (logCategory.isNull() && !state->defaultCategory.isNull())
            logCategory = state->defaultCategory;
        if (!logCategory.isNull())
            selected = state->categoryAppenders.values(logCategory);
        categorySelected = !selected.isEmpty();
        if (!categorySelected)
            selected = state->appenders;
    }
    // Never hold a logger/global mutex while invoking user code or doing I/O.
    // Slots synchronize only the selected appender with unregister/destruction.
    for (const Slot &slot : selected) {
        std::lock_guard<std::recursive_mutex> callback(slot->mutex);
        {
            std::lock_guard<std::mutex> locker(state->mutex);
            if (!state->alive || !slot->appender)
                continue;
            if (categorySelected
                ? !state->categoryAppenders.contains(logCategory, slot)
                : !state->appenders.contains(slot))
                continue;
        }
        slot->appender->write(time, level, file, line, func, logCategory, message);
    }
    if (!logCategory.isNull() && !state->global) {
        const QByteArray categoryData = logCategory.toLatin1();
        dispatch(globalState(), time, level, file, line, func,
                 categoryData.constData(), message, true);
    }
    if (selected.isEmpty() && !forwarded) {
        const QString fallback = QStringLiteral("[%1] <%2> %3")
            .arg(Logger::levelToString(level), -7)
            .arg(AbstractStringAppender::stripFunctionName(func), message);
        std::cerr << qPrintable(fallback) << std::endl;
    }
    if (level == Logger::Fatal && !forwarded) {
        std::cerr << "Fatal log record received; aborting" << std::endl;
        std::abort();
    }
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    const char *category = context.category;
    if (category && qstrcmp(category, "default") == 0)
        category = nullptr;
    dispatch(globalState(), QDateTime::currentDateTime(), qtLevel(type),
             context.file, context.line, context.function, category, message);
}

Slot registeredSlot(LoggerState &state, AbstractAppender *appender)
{
    Slot &slot = state.registrations[appender];
    if (!slot)
        slot = std::make_shared<AppenderSlot>(appender);
    return slot;
}

// Called with state.mutex held; invalidate outside it to preserve lock order.
Slot releaseSlot(LoggerState &state, AbstractAppender *appender)
{
    const Slot slot = state.registrations.value(appender);
    if (state.appenders.contains(slot) || state.categoryAppenders.values().contains(slot))
        return Slot();
    return state.registrations.take(appender);
}

void invalidate(const Slot &slot)
{
    if (slot) {
        std::lock_guard<std::recursive_mutex> callback(slot->mutex);
        slot->appender = nullptr;
    }
}
} // namespace

Logger::Logger() : d_ptr(new LoggerPrivate) {}
Logger::Logger(const QString &category) : Logger() { setDefaultCategory(category); }

Logger::~Logger()
{
    Q_D(Logger);
    const auto state = d->state;
    QList<Slot> owned;
    {
        std::lock_guard<std::mutex> locker(state->mutex);
        state->alive = false;
        owned = state->registrations.values();
        state->appenders.clear();
        state->categoryAppenders.clear();
        state->registrations.clear();
    }
    for (const Slot &slot : owned) {
        std::lock_guard<std::recursive_mutex> callback(slot->mutex);
        delete slot->appender;
        slot->appender = nullptr;
    }
    // Pending streams own state, never Logger*. Their final callback observes
    // alive=false, even if it runs concurrently with or after this destructor.
    delete d_ptr;
}

Logger *Logger::globalInstance()
{
    {
        QReadLocker locker(&LoggerPrivate::globalInstanceLock);
        if (LoggerPrivate::globalInstance)
            return LoggerPrivate::globalInstance;
    }
    QWriteLocker locker(&LoggerPrivate::globalInstanceLock);
    if (!LoggerPrivate::globalInstance) {
        LoggerPrivate::globalInstance = new Logger;
        LoggerPrivate::globalState = LoggerPrivate::globalInstance->d_ptr->state;
        LoggerPrivate::globalState->global = true;
        installMessageHandler();
        qAddPostRoutine(cleanupLoggerGlobalInstance);
    }
    return LoggerPrivate::globalInstance;
}

QString Logger::levelToString(Logger::LogLevel level)
{
    switch (level) {
    case Trace:
        return QStringLiteral("Trace");
    case Debug:
        return QStringLiteral("Debug");
    case Info:
        return QStringLiteral("Info");
    case Warning:
        return QStringLiteral("Warning");
    case Error:
        return QStringLiteral("Error");
    case Fatal:
        return QStringLiteral("Fatal");
    }
    return QString();
}

Logger::LogLevel Logger::levelFromString(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("trace"))
        return Trace;
    if (normalized == QLatin1String("info"))
        return Info;
    if (normalized == QLatin1String("warning"))
        return Warning;
    if (normalized == QLatin1String("error"))
        return Error;
    if (normalized == QLatin1String("fatal"))
        return Fatal;
    return Debug;
}

void Logger::registerAppender(AbstractAppender *appender)
{
    if (!appender)
        return;
    const auto state = d_ptr->state;
    std::lock_guard<std::mutex> locker(state->mutex);
    const Slot slot = registeredSlot(*state, appender);
    if (state->appenders.contains(slot)) {
        std::cerr << "Appender is already registered" << std::endl;
        return;
    }
    state->appenders.append(slot);
}

void Logger::unregisterAppender(AbstractAppender *appender)
{
    const auto state = d_ptr->state;
    Slot released;
    {
        std::lock_guard<std::mutex> locker(state->mutex);
        state->appenders.removeAll(state->registrations.value(appender));
        released = releaseSlot(*state, appender);
    }
    invalidate(released);
}

void Logger::registerCategoryAppender(const QString &category, AbstractAppender *appender)
{
    if (!appender)
        return;
    const auto state = d_ptr->state;
    std::lock_guard<std::mutex> locker(state->mutex);
    const Slot slot = registeredSlot(*state, appender);
    if (state->categoryAppenders.contains(category, slot)) {
        std::cerr << "Category appender is already registered: " << qPrintable(category) << std::endl;
        return;
    }
    state->categoryAppenders.insert(category, slot);
}

void Logger::unregisterCategoryAppender(const QString &category, AbstractAppender *appender)
{
    const auto state = d_ptr->state;
    Slot released;
    {
        std::lock_guard<std::mutex> locker(state->mutex);
        state->categoryAppenders.remove(category, state->registrations.value(appender));
        released = releaseSlot(*state, appender);
    }
    invalidate(released);
}

void Logger::logToGlobalInstance(const QString &, bool)
{
    std::cerr << "Logger::logToGlobalInstance is deprecated and has no effect" << std::endl;
}

void Logger::setDefaultCategory(const QString &category)
{
    std::lock_guard<std::mutex> locker(d_ptr->state->mutex);
    d_ptr->state->defaultCategory = category;
}

QString Logger::defaultCategory() const
{
    Q_D(const Logger);
    std::lock_guard<std::mutex> locker(d->state->mutex);
    return d->state->defaultCategory;
}

void Logger::write(const QDateTime &time, Logger::LogLevel level,
                   const char *file, int line, const char *func,
                   const char *category, const QString &message)
{
    write(time, level, file, line, func, category, message, false);
}

void Logger::write(Logger::LogLevel level, const char *file, int line,
                   const char *func, const char *category, const QString &message)
{
    write(QDateTime::currentDateTime(), level, file, line, func, category, message);
}

QDebug Logger::write(Logger::LogLevel level, const char *file, int line,
                     const char *func, const char *category)
{
    globalInstance();
    const auto state = d_ptr->state;
    const QByteArray fileData(file), functionData(func), categoryData(category);
    const bool hasFile = file, hasFunction = func, hasCategory = category;
    return detail::completedStream(
        [state, level, fileData, functionData, categoryData, hasFile,
         hasFunction, hasCategory, line](const QString &message) {
            dispatch(state, QDateTime::currentDateTime(), level,
                     hasFile ? fileData.constData() : nullptr, line,
                     hasFunction ? functionData.constData() : nullptr,
                     hasCategory ? categoryData.constData() : nullptr, message);
        });
}

void Logger::writeAssert(const char *file, int line, const char *func, const char *condition)
{
    write(Fatal, file, line, func, nullptr,
          QStringLiteral("ASSERT: \"%1\"").arg(QString::fromLocal8Bit(condition ? condition : "")));
}

void Logger::write(const QDateTime &time, Logger::LogLevel level,
                   const char *file, int line, const char *func,
                   const char *category, const QString &message, bool forwarded)
{
    globalInstance();
    dispatch(d_ptr->state, time, level, file, line, func, category, message, forwarded);
}

Logger *loggerInstance()
{
    return Logger::globalInstance();
}

class DLOG_DECL_DEPRECATED CuteMessageLogger
{
    Q_DISABLE_COPY(CuteMessageLogger)
public:
    Q_DECL_CONSTEXPR CuteMessageLogger(Logger *, Logger::LogLevel,
                                       const char *, int, const char *)
    {
    }

    Q_DECL_CONSTEXPR CuteMessageLogger(Logger *, Logger::LogLevel,
                                       const char *, int, const char *,
                                       const char *)
    {
    }

    void write(const char *, ...) const;
    void write(const QString &) const;
    QDebug write() const;
};

void CuteMessageLogger::write(const char *, ...) const
{
    write();
}

void CuteMessageLogger::write(const QString &) const
{
    write();
}

QDebug CuteMessageLogger::write() const
{
    return QDebug(QtWarningMsg)
        << "Deprecated logger API; rebuild the caller against dtklog";
}

DLOG_CORE_END_NAMESPACE
