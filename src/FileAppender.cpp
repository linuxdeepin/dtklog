// SPDX-FileCopyrightText: 2010 Boris Moiseev (cyberbobs at gmail dot com)
//
// SPDX-License-Identifier: LGPL-2.1-or-later

/*
  Copyright (c) 2010 Boris Moiseev (cyberbobs at gmail dot com)

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1
  as published by the Free Software Foundation and appearing in the file
  LICENSE.LGPL included in the packaging of this file.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.
*/

#include "FileAppender.h"

#include <QFileInfo>
#include <QGlobalStatic>
#include <stdexcept>

#include "fileappender_p.h"
#include <map>
#include <memory>

#include <iostream>

DLOG_CORE_BEGIN_NAMESPACE

namespace {
struct FileState {
    std::mutex mutex;
    QString path;
    std::size_t sizeLimit = 20 * 1024 * 1024;
    std::size_t countLimit = 0;
    RollingInterval interval = RI_Daily;
    std::shared_ptr<spdlog::logger> logger;
};
struct FileRegistry {
    std::mutex mutex;
    std::map<const FileAppender *, std::shared_ptr<FileState>> states;
};
Q_GLOBAL_STATIC(FileRegistry, fileRegistryStorage)

FileRegistry &fileRegistry()
{
    if (fileRegistryStorage.isDestroyed())
        throw std::runtime_error("file logging has already shut down");
    return *fileRegistryStorage();
}
std::shared_ptr<FileState> fileState(const FileAppender *appender)
{
    // The weak cache avoids a process-wide registry lock on every record;
    // erasing a destroyed appender expires its cache even if its address is reused.
    static thread_local const FileAppender *lastAppender = nullptr;
    static thread_local std::weak_ptr<FileState> lastState;
    if (lastAppender == appender) {
        if (auto cached = lastState.lock())
            return cached;
    }
    auto &registry = fileRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto &state = registry.states[appender];
    if (!state)
        state = std::make_shared<FileState>();
    lastAppender = appender;
    lastState = state;
    return state;
}
void changeFilePath(const FileAppender *appender, const QString &path)
{
    const auto state = fileState(appender);
    std::shared_ptr<spdlog::logger> released;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->path = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
        released = std::move(state->logger);
    }
}
void reportFileError(const std::exception &error)
{
    std::fprintf(stderr, "dtklog: cannot write file log: %s\n", error.what());
}
} // namespace

// Kept for existing exported-symbol compatibility. Different directories must
// never share a logger solely because their files have the same basename.
std::string loggerName(const QFile &logFile)
{
    return QFileInfo(logFile).absoluteFilePath().toStdString();
}

namespace detail {
std::shared_ptr<spdlog::logger> fileLogger(const FileAppender *appender)
{
    const auto state = fileState(appender);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->path.isEmpty())
        return {};
    if (!state->logger) {
        auto sink = std::make_shared<rolling_file_sink_mt>(state->path.toStdString(),
                    state->sizeLimit, state->countLimit, false, state->interval);
        // A private, unregistered logger avoids global spdlog registry lifetime
        // coupling. Other appenders keep their own sink and file descriptor.
        auto logger = std::make_shared<spdlog::logger>(state->path.toStdString(), sink);
        logger->set_level(spdlog::level::trace);
        logger->set_error_handler([](const std::string &error) {
            std::fprintf(stderr, "dtklog: cannot write file log: %s\n", error.c_str());
        });
        state->logger = logger;
    }
    return state->logger;
}
void setFileSizeLimit(const FileAppender *appender, std::size_t limit)
{
    const auto state = fileState(appender);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->sizeLimit = limit;
    if (state->logger)
        static_cast<rolling_file_sink_mt *>(state->logger->sinks().front().get())->set_max_size(limit);
}
void setFileCountLimit(const FileAppender *appender, std::size_t limit)
{
    const auto state = fileState(appender);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->countLimit = limit;
    if (state->logger)
        static_cast<rolling_file_sink_mt *>(state->logger->sinks().front().get())->set_max_files(limit);
}
void setFileInterval(const FileAppender *appender, RollingInterval interval)
{
    const auto state = fileState(appender);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->interval = interval;
    if (state->logger)
        static_cast<rolling_file_sink_mt *>(state->logger->sinks().front().get())->set_interval(interval);
}
} // namespace detail

/*!
@~english
  @class Dtk::Core::FileAppender
  @ingroup dtkcore

  @brief Simple appender that writes the log records to the plain text file.
 */

/*!
@~english
    @brief Constructs the new file appender assigned to file with the given \a fileName.
 */
FileAppender::FileAppender(const QString &fileName)
{
    setFileName(fileName);
}

FileAppender::~FileAppender()
{
    // A static Logger can own appenders created after it was constructed.
    // Their registry may already have been destroyed during process shutdown.
    if (fileRegistryStorage.isDestroyed())
        return;
    std::shared_ptr<FileState> released;
    auto &registry = fileRegistry();
    {
        std::lock_guard<std::mutex> lock(registry.mutex);
        const auto found = registry.states.find(this);
        if (found != registry.states.end()) {
            released = std::move(found->second);
            registry.states.erase(found);
        }
    }
}

/*!
@~english
  \brief Returns the name set by setFileName() or to the FileAppender constructor.

  \sa setFileName()
 */
QString FileAppender::fileName() const
{
    QMutexLocker locker(&m_logFileMutex);
    return m_logFile.fileName();
}

/*!
  \brief Sets the \a s name of the file. The name can have no path, a relative path, or an absolute path.

  \sa fileName()
 */
void FileAppender::setFileName(const QString &s)
{
    QMutexLocker locker(&m_logFileMutex);

    if (s == m_logFile.fileName())
        return;

    m_logFile.setFileName(s);

    changeFilePath(this, s);
}

qint64 FileAppender::size() const
{
    QMutexLocker locker(&m_logFileMutex);

    try {
        if (const auto logger = detail::fileLogger(this))
            return qint64(static_cast<rolling_file_sink_mt *>(logger->sinks().front().get())->filesize());
    } catch (const std::exception &error) {
        reportFileError(error);
    }
    return m_logFile.size();
}

bool FileAppender::openFile()
{
    QMutexLocker locker(&m_logFileMutex);
    try {
        return bool(detail::fileLogger(this));
    } catch (const std::exception &error) {
        reportFileError(error);
        return false;
    }
}

/*!
@~english
  \brief Write the log record to the file.
  \reimp

  The \a time parameter indicates the time stamp.
  The \a level parameter describes the LogLevel.
  The \a file parameter is the current file name.
  The \a line parameter indicates the number of lines to output.
  The \a func parameter indicates the func name to output.
  The \a category parameter indicates the log category.
  The \a msg parameter indicates the output message.

  \sa fileName()
  \sa AbstractStringAppender::format()
 */
void FileAppender::append(const QDateTime &time, Logger::LogLevel level, const char *file, int line,
                          const char *func, const QString &category, const QString &msg)
{
    // Custom format() implementations may inspect this appender's settings.
    // Render once, without holding the filename/configuration mutexes.
    const QString formatted = formattedString(time, level, file, line, func, category, msg);
    try {
        std::shared_ptr<spdlog::logger> logger;
        {
            QMutexLocker locker(&m_logFileMutex);
            logger = detail::fileLogger(this);
        }
        if (logger)
            logger->log(spdlog::level::level_enum(level), formatted.toStdString());
    } catch (const std::exception &error) {
        reportFileError(error);
    }
}

void FileAppender::closeFile()
{
    std::shared_ptr<spdlog::logger> released;
    const auto state = fileState(this);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        released = std::move(state->logger);
    }
}

DLOG_CORE_END_NAMESPACE
