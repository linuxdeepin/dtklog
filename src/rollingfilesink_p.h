// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/details/file_helper.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/os.h>

#include "rollingfile_p.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef SPDLOG_VERSION_CHECK
#define SPDLOG_VERSION_CHECK(major, minor, patch) (major * 10000 + minor * 100 + patch)
#endif

DLOG_CORE_BEGIN_NAMESPACE

using namespace spdlog;

enum RollingInterval {
    RI_Minutely = 0, RI_Hourly, RI_HalfDaily, RI_Daily, RI_Weekly, RI_Monthly
};

inline QString rollingDatePattern(RollingInterval interval)
{
    switch (interval) {
    case RI_Minutely: return QStringLiteral(".yyyy-MM-dd-hh-mm");
    case RI_Hourly: return QStringLiteral(".yyyy-MM-dd-hh");
    case RI_HalfDaily: return QStringLiteral(".yyyy-MM-dd-ap");
    case RI_Daily: return QStringLiteral(".yyyy-MM-dd");
    case RI_Weekly: return QStringLiteral(".yyyy-ww");
    case RI_Monthly: return QStringLiteral(".yyyy-MM");
    }
    return QStringLiteral(".yyyy-MM-dd");
}

// A persistent sidecar inode coordinates both independent sinks and processes.
// It must not be unlinked when a writer exits: another writer may still hold it.
class LogFileLock {
public:
    explicit LogFileLock(const QString &path) : m_path(path) {}
    ~LogFileLock()
    {
#ifdef Q_OS_UNIX
        if (m_fd >= 0)
            ::close(m_fd);
#endif
    }
    void lock()
    {
#ifdef Q_OS_UNIX
        if (m_pid != ::getpid()) {
            if (m_fd >= 0)
                ::close(m_fd);
            m_fd = -1;
            m_pid = ::getpid();
        }
        if (m_fd < 0) {
            const QFileInfo info(m_path);
            const QByteArray path = QFile::encodeName(
                info.dir().filePath(QLatin1Char('.') + info.fileName() + QStringLiteral(".lock")));
            m_fd = ::open(path.constData(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (m_fd < 0)
                throw spdlog_ex("cannot open log lock " + m_path.toStdString(), errno);
        }
        while (::flock(m_fd, LOCK_EX) != 0) {
            if (errno != EINTR)
                throw spdlog_ex("cannot lock log file " + m_path.toStdString(), errno);
        }
#endif
    }
    void unlock() noexcept
    {
#ifdef Q_OS_UNIX
        while (::flock(m_fd, LOCK_UN) != 0 && errno == EINTR) {}
#endif
    }
private:
    LogFileLock(const LogFileLock &) = delete;
    LogFileLock &operator=(const LogFileLock &) = delete;
    QString m_path;
#ifdef Q_OS_UNIX
    int m_fd = -1;
    pid_t m_pid = 0;
#endif
};

// DTK has already formatted the message. Preserve explicit newlines, adding
// just one record terminator when the caller's format has none.
class LogRecordFormatter final : public formatter {
public:
    void format(const details::log_msg &msg, memory_buf_t &dest) override
    {
        if (msg.payload.size() > 0)
            dest.append(msg.payload.data(), msg.payload.data() + msg.payload.size());
        if (msg.payload.size() == 0 || msg.payload.data()[msg.payload.size() - 1] != '\n')
            dest.push_back('\n');
    }
    std::unique_ptr<formatter> clone() const override
    {
        return std::unique_ptr<formatter>(new LogRecordFormatter);
    }
};

// The sink owns all file I/O through spdlog::details::file_helper. Its mutex
// protects one instance; LogFileLock protects the shared path across instances.
template <typename Mutex>
class rolling_file_sink final : public sinks::base_sink<Mutex> {
public:
    rolling_file_sink(filename_t filename, std::size_t maxSize, std::size_t maxFiles,
                      bool rollingOnOpen = false, RollingInterval interval = RI_Daily)
        : m_path(QString::fromStdString(filename)), m_nativePath(QFile::encodeName(m_path)),
          m_fileLock(m_path),
          m_maxSize(maxSize), m_maxFiles(maxFiles), m_interval(interval)
#if SPDLOG_VERSION >= SPDLOG_VERSION_CHECK(1, 5, 0)
        , m_file(fileEvents())
#endif
    {
        if (maxSize == 0 || maxFiles > 200000)
            throw spdlog_ex("invalid rolling log limits");
        // Create the parent before opening its sidecar lock. All rotation and
        // record writes (including their flush) take that lock below.
        if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
            throw spdlog_ex("cannot create log directory " + m_path.toStdString());
        std::lock_guard<LogFileLock> operation(m_fileLock);
        openCurrentFile();
        if (rollingOnOpen && m_size > 0)
            rotate();
        this->set_formatter(std::unique_ptr<formatter>(new LogRecordFormatter));
    }

    filename_t filename()
    {
        std::lock_guard<Mutex> guard(this->mutex_);
        return m_path.toStdString();
    }
    std::size_t filesize()
    {
        std::lock_guard<Mutex> guard(this->mutex_);
        std::lock_guard<LogFileLock> operation(m_fileLock);
        refreshCurrentFile();
        return m_size;
    }
    void set_max_files(std::size_t limit)
    {
        if (limit > 200000)
            throw spdlog_ex("log file count cannot exceed 200000");
        std::lock_guard<Mutex> guard(this->mutex_);
        m_maxFiles = limit;
    }
    void set_max_size(std::size_t limit)
    {
        if (!limit)
            throw spdlog_ex("log size limit cannot be zero");
        std::lock_guard<Mutex> guard(this->mutex_);
        m_maxSize = limit;
    }
    void set_interval(RollingInterval interval)
    {
        std::lock_guard<Mutex> guard(this->mutex_);
        m_interval = interval;
        updateDeadline();
    }

protected:
    void sink_it_(const details::log_msg &msg) override
    {
        memory_buf_t formatted;
        this->formatter_->format(msg, formatted);
        std::lock_guard<LogFileLock> operation(m_fileLock);
        try {
            refreshCurrentFile();
            const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
                msg.time.time_since_epoch()).count();
            const bool expired = m_nextRotationMs > 0 && now >= m_nextRotationMs;
            const bool oversized = formatted.size() > m_maxSize
                || m_size > m_maxSize - formatted.size();
            const bool rotated = m_size > 0 && (expired || oversized);
            if (rotated)
                rotate();
            m_file.write(formatted);
            // Flush before releasing the process lock, so another process's
            // size check includes this record and cannot rotate buffered data.
            m_file.flush();
            m_size += formatted.size();
            if (rotated && m_maxFiles > 0)
                removeOldFiles();
        } catch (...) {
            // A failed stdio stream must not poison every subsequent record.
            m_file.close();
            m_open = false;
            throw;
        }
    }
    void flush_() override
    {
        std::lock_guard<LogFileLock> operation(m_fileLock);
        if (m_open)
            m_file.flush();
    }

private:
#if SPDLOG_VERSION >= SPDLOG_VERSION_CHECK(1, 5, 0)
    file_event_handlers fileEvents()
    {
        file_event_handlers events;
#ifdef Q_OS_UNIX
        events.after_open = [this](const filename_t &, std::FILE *file) {
            struct stat state;
            if (::fstat(::fileno(file), &state) != 0)
                throw spdlog_ex("cannot stat open log file", errno);
            m_device = state.st_dev;
            m_inode = state.st_ino;
        };
#endif
        return events;
    }
#endif
    QDateTime nextRotation(const QDateTime &start) const
    {
        return detail::nextRollover(static_cast<RollingFileAppender::DatePattern>(m_interval), start);
    }
    void updateDeadline()
    {
        const QDateTime next = nextRotation(m_periodStart);
        m_nextRotationMs = next.isValid() ? next.toMSecsSinceEpoch() : 0;
    }
    void resetPeriod(const QFileInfo &info)
    {
        m_periodStart = info.size() > 0 && info.lastModified().isValid()
            ? info.lastModified() : QDateTime::currentDateTime();
        updateDeadline();
    }
    void openCurrentFile()
    {
        m_file.open(m_path.toStdString(), false);
        m_open = true;
        const QFileInfo info(m_path);
        m_size = m_file.size();
#if defined(Q_OS_UNIX) && SPDLOG_VERSION < SPDLOG_VERSION_CHECK(1, 5, 0)
        struct stat state;
        if (::stat(m_nativePath.constData(), &state) != 0)
            throw spdlog_ex("cannot stat log path", errno);
        m_device = state.st_dev;
        m_inode = state.st_ino;
#endif
        resetPeriod(info);
    }
    void refreshCurrentFile()
    {
        if (!m_open) {
            openCurrentFile();
            return;
        }
#ifdef Q_OS_UNIX
        struct stat state;
        if (::stat(m_nativePath.constData(), &state) != 0) {
            if (errno != ENOENT)
                throw spdlog_ex("cannot stat log path " + m_path.toStdString(), errno);
            openCurrentFile();
        } else if (state.st_dev != m_device || state.st_ino != m_inode) {
            openCurrentFile();
        } else {
            const std::size_t size = static_cast<std::size_t>(state.st_size);
            if (size < m_size)
                resetPeriod(QFileInfo(m_path));
            m_size = size;
        }
#else
        const QFileInfo info(m_path);
        if (!info.exists())
            openCurrentFile();
        else
            m_size = static_cast<std::size_t>(info.size());
#endif
    }
    QString archiveSuffix() const
    {
        if (m_interval == RI_Weekly) {
            int year = 0;
            const int week = m_periodStart.date().weekNumber(&year);
            return QStringLiteral(".%1-%2").arg(year, 4, 10, QLatin1Char('0'))
                .arg(week, 2, 10, QLatin1Char('0'));
        }
        return m_periodStart.toString(rollingDatePattern(m_interval));
    }
    void rotate()
    {
        m_file.close();
        m_open = false;
        const QString prefix = m_path + archiveSuffix();
        if (prefix != m_archivePrefix) {
            m_archivePrefix = prefix;
            m_sequence = 0;
        }
        for (;;) {
            const QString candidate = prefix + (m_sequence ? QLatin1Char('.') + QString::number(m_sequence) : QString());
            if (m_sequence == std::numeric_limits<quint64>::max())
                throw spdlog_ex("log archive sequence exhausted");
            ++m_sequence;
            const QFileInfo destination(candidate);
            if (destination.exists() || destination.isSymLink())
                continue;
            QFile source(m_path);
            // QFile::rename refuses an existing destination. Never truncate
            // the active file or overwrite a previous archive on failure.
            if (source.rename(candidate))
                break;
            const QFileInfo after(candidate);
            if (after.exists() || after.isSymLink())
                continue;
            throw spdlog_ex("cannot archive log " + m_path.toStdString()
                            + ": " + source.errorString().toStdString());
        }
        openCurrentFile();
    }
    static bool isArchiveDate(const QString &suffix)
    {
        const QStringList patterns {
            QStringLiteral(".yyyy-MM-dd-hh-mm-ss"), QStringLiteral(".yyyy-MM-dd-hh-mm"),
            QStringLiteral(".yyyy-MM-dd-hh"), QStringLiteral(".yyyy-MM-dd-ap"),
            QStringLiteral(".yyyy-MM-dd"), QStringLiteral(".yyyy-MM")
        };
        for (const QString &pattern : patterns) {
            const QDateTime parsed = QDateTime::fromString(suffix, pattern);
            if (parsed.isValid() && parsed.toString(pattern) == suffix)
                return true;
        }
        // ISO week-year + week number; Qt's date format has no week token.
        if (suffix.size() == 8) {
            const int year = suffix.mid(1, 4).toInt();
            const int week = suffix.mid(6, 2).toInt();
            return QDate(year, 12, 28).isValid()
                && week >= 1 && week <= QDate(year, 12, 28).weekNumber();
        }
        return false;
    }
    void removeOldFiles()
    {
        const QFileInfo active(m_path);
        const QDir dir(active.absolutePath());
        // Recognize the six supported calendar patterns and historical
        // seconds-resolution archives. Arbitrary prefix matches are unsafe.
        const QRegularExpression archivePattern(
            QLatin1Char('^') + QRegularExpression::escape(active.fileName())
            + QStringLiteral("(\\.\\d{4}-\\d{2}(?:-\\d{2}(?:-\\d{2}(?:-\\d{2}(?:-\\d{2})?)?|-am|-pm)?)?)(?:\\.(\\d+))?$"));
        QFileInfoList archives;
        const auto entries = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            const auto match = archivePattern.match(entry.fileName());
            if (match.hasMatch() && isArchiveDate(match.captured(1)))
                archives.append(entry);
        }
        std::sort(archives.begin(), archives.end(), [&archivePattern](const QFileInfo &a, const QFileInfo &b) {
            if (a.lastModified() != b.lastModified())
                return a.lastModified() > b.lastModified();
            const auto am = archivePattern.match(a.fileName());
            const auto bm = archivePattern.match(b.fileName());
            if (am.captured(1) != bm.captured(1))
                return am.captured(1) > bm.captured(1);
            return am.captured(2).toULongLong() > bm.captured(2).toULongLong();
        });
        // Preserve the existing API: the limit includes the active log.
        for (int i = static_cast<int>(m_maxFiles - 1); i < archives.size(); ++i) {
            const QFileInfo entry(archives.at(i).absoluteFilePath());
            if (!entry.isSymLink() && entry.isFile() && !QFile::remove(entry.absoluteFilePath()))
                throw spdlog_ex("cannot remove log archive " + entry.absoluteFilePath().toStdString());
        }
    }

    QString m_path;
    QByteArray m_nativePath;
    LogFileLock m_fileLock;
    std::size_t m_maxSize;
    std::size_t m_maxFiles;
    std::size_t m_size = 0;
    RollingInterval m_interval;
    QDateTime m_periodStart;
    qint64 m_nextRotationMs = 0;
    QString m_archivePrefix;
    quint64 m_sequence = 0;
    bool m_open = false;
#ifdef Q_OS_UNIX
    dev_t m_device = 0;
    ino_t m_inode = 0;
#endif
    details::file_helper m_file;
};

using rolling_file_sink_mt = rolling_file_sink<std::mutex>;
using rolling_file_sink_st = rolling_file_sink<details::null_mutex>;

DLOG_CORE_END_NAMESPACE
