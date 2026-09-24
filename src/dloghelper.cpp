// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "dloghelper.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <iostream>

DLOG_CORE_BEGIN_NAMESPACE

struct DLogHelperData
{
    const char *file = nullptr;
    const char *function = nullptr;
    const char *category = nullptr;
    int line = 0;
    Logger::LogLevel level = Logger::LogLevel::Debug;

    void setContext(const QMessageLogContext &ctx)
    {
        file = ctx.file;
        line = ctx.line;
        function = ctx.function;
        category = ctx.category;
    }
};

DLogHelper::DLogHelper(Logger::LogLevel level, const QMessageLogContext &context, QObject *parent)
    : QObject(parent)
    , m_data(new DLogHelperData)
{
    m_data->setContext(context);
    m_data->level = level;
}

DLogHelper::~DLogHelper()
{
    delete m_data;
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
    Logger::globalInstance()->write(m_data->level, m_data->file, m_data->line,
                                    m_data->function, m_data->category, msg);
}

QDebug DLogHelper::write()
{
    return Logger::globalInstance()->write(m_data->level, m_data->file, m_data->line,
                                           m_data->function, m_data->category);
}

void DLogHelper::timing(const QString &msg, QObject *context /* = nullptr*/)
{
    if (!context)
        context = this;

    QElapsedTimer *elapsedTimer = new QElapsedTimer;
    elapsedTimer->start();
    QObject::connect(context, &QObject::destroyed, [elapsedTimer, msg, this]()
                     {
        QString message;
        message = msg + (QLatin1String(" finished in "));

        qint64 elapsed = elapsedTimer->elapsed();
        delete elapsedTimer;

        if (elapsed >= 10000)
            message += QString::number(elapsed / 1000) + QLatin1String("s.");
        else
            message += QString::number(elapsed) + QLatin1String("ms.");

        write(message); });
}

Logger::LogLevel DLogHelper::levelFromQtMsgType(QtMsgType mt)
{
    Logger::LogLevel level;
    switch (mt)
    {
    case QtDebugMsg:
        level = Logger::Debug;
        break;
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
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
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
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
