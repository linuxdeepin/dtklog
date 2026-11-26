// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dloghelper.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <private/qobject_p.h>

#include <memory>

DLOG_CORE_BEGIN_NAMESPACE

class DLogHelperPrivate : public QObjectPrivate
{
public:
    // Functions.
    explicit DLogHelperPrivate(decltype(QObjectPrivateVersion) version = QObjectPrivateVersion)
        : QObjectPrivate(version)
    {
    }
    ~DLogHelperPrivate()
    {
    }

    void setContext(const QMessageLogContext &ctx)
    {
        fileStorage = ctx.file;
        functionStorage = ctx.function;
        categoryStorage = ctx.category;
        file = ctx.file ? fileStorage.constData() : nullptr;
        line = ctx.line;
        function = ctx.function ? functionStorage.constData() : nullptr;
        category = ctx.category ? categoryStorage.constData() : nullptr;
    }
    void setLevel(Logger::LogLevel lvl)
    {
        level = lvl;
    }

    const char *file = nullptr;
    const char *function = nullptr;
    const char *category = nullptr;
    QByteArray fileStorage, functionStorage, categoryStorage;
    int line = 0;
    Logger::LogLevel level = Logger::LogLevel::Debug;
};

DLogHelper::DLogHelper(Logger::LogLevel level, const QMessageLogContext &context, QObject *parent)
    : QObject(*new DLogHelperPrivate, parent)
{
    d_func()->setContext(context);
    d_func()->setLevel(level);
}

DLogHelper::~DLogHelper()
{
}

void DLogHelper::write(const char *msg, ...)
{
    QString message;
    va_list va;
    va_start(va, msg);
    message = QString::vasprintf(msg, va);
    va_end(va);

    write(message);
}

void DLogHelper::write(const QString &msg)
{
    Q_D(DLogHelper);
    Logger::globalInstance()->write(d->level, d->file, d->line,
                                    d->function, d->category, msg);
}

QDebug DLogHelper::write()
{
    Q_D(DLogHelper);
    return Logger::globalInstance()->write(d->level, d->file, d->line,
                                           d->function, d->category);
}

void DLogHelper::timing(const QString &msg, QObject *context /* = nullptr*/)
{
    Q_D(DLogHelper);
    if (!context)
        context = this;

    // The context can outlive this helper. Capture values, never its address.
    const QByteArray file(d->file), function(d->function), category(d->category);
    const bool hasFile = d->file, hasFunction = d->function, hasCategory = d->category;
    const int line = d->line;
    const Logger::LogLevel level = d->level;
    const auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    QObject::connect(context, &QObject::destroyed,
                     [timer, msg, file, function, category, hasFile,
                      hasFunction, hasCategory, line, level]() {
        const qint64 elapsed = timer->elapsed();
        const QString duration = elapsed >= 10000
            ? QString::number(elapsed / 1000) + QLatin1String("s.")
            : QString::number(elapsed) + QLatin1String("ms.");
        Logger::globalInstance()->write(
            level, hasFile ? file.constData() : nullptr, line,
            hasFunction ? function.constData() : nullptr,
            hasCategory ? category.constData() : nullptr,
            msg + QLatin1String(" finished in ") + duration);
    });
}

Logger::LogLevel DLogHelper::levelFromQtMsgType(QtMsgType mt)
{
    Logger::LogLevel level = Logger::Warning;
    switch (mt)
    {
    case QtDebugMsg:
        level = Logger::Debug;
        break;
#if QT_VERSION >= 0x050500
    case QtInfoMsg:
        level = Logger::Info;
        break;
#endif
    case QtWarningMsg:
        level = Logger::Warning;
        break;
    case QtCriticalMsg:
        level = Logger::Error;
        break;
    case QtFatalMsg:
        level = Logger::Fatal;
        break;
    default:
        break;
    }
    return level;
}

QtMsgType DLogHelper::qtMsgTypeFromLogLevel(Logger::LogLevel lvl)
{
    QtMsgType mt;
    switch (lvl)
    {
    case Logger::Debug:
        mt = QtDebugMsg;
        break;
#if QT_VERSION >= 0x050500
    case Logger::Info:
        mt = QtInfoMsg;
        break;
#endif
    case Logger::Warning:
        mt = QtWarningMsg;
        break;
    case Logger::Error:
        mt = QtCriticalMsg;
        break;
    case Logger::Fatal:
        mt = QtFatalMsg;
        break;
    default:
        mt = QtWarningMsg;
    }
    return mt;
}

DLOG_CORE_END_NAMESPACE
