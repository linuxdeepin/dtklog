// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <AbstractAppender.h>
#include <Logger.h>

#include <QCoreApplication>
#include <QDebug>
#include <QSemaphore>

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace Dtk::Core;

static std::atomic<int> fallbackMessages{0};
static std::atomic<int> appendedMessages{0};
static std::atomic<int> destroyedAppenders{0};
static QtMessageHandler capturedHandler = nullptr;

static void fallbackHandler(QtMsgType, const QMessageLogContext &, const QString &)
{
    ++fallbackMessages;
}

static void forwardingHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    capturedHandler(type, context, message);
}

class CountingAppender : public AbstractAppender
{
public:
    ~CountingAppender() override
    {
        ++destroyedAppenders;
        qWarning("appender destroyed");
    }

protected:
    void append(const QDateTime &, Logger::LogLevel, const char *, int,
                const char *, const QString &, const QString &) override
    {
        ++appendedMessages;
    }
};

int main(int argc, char **argv)
{
    qInstallMessageHandler(fallbackHandler);
    auto app = std::unique_ptr<QCoreApplication>(new QCoreApplication(argc, argv));
    const bool initialize = argc == 2 && QByteArray(argv[1]) == "initialize";

    if (initialize) {
        QSemaphore ready;
        QSemaphore start;
        std::vector<Logger *> instances(16);
        std::vector<std::thread> threads;
        for (size_t i = 0; i < instances.size(); ++i) {
            threads.emplace_back([&, i] {
                ready.release();
                start.acquire();
                instances[i] = Logger::globalInstance();
            });
        }
        ready.acquire(int(instances.size()));
        start.release(int(instances.size()));
        for (auto &thread : threads)
            thread.join();
        for (auto instance : instances) {
            if (instance != instances.front())
                return 1;
        }
        return 0;
    }

    Logger::globalInstance()->registerAppender(new CountingAppender);
    // Preserve a callback fetched before shutdown, as Qt can do on another
    // thread immediately before invoking the installed message handler.
    capturedHandler = qInstallMessageHandler(fallbackHandler);
    const bool chained = argc == 2 && QByteArray(argv[1]) == "shutdown-chained";
    qInstallMessageHandler(chained ? forwardingHandler : capturedHandler);

    std::atomic<bool> running{true};
    QSemaphore ready;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&] {
            capturedHandler(QtDebugMsg, QMessageLogContext(), QStringLiteral("before application shutdown"));
            ready.release();
            while (running)
                capturedHandler(QtDebugMsg, QMessageLogContext(), QStringLiteral("concurrent application shutdown"));
        });
    }
    ready.acquire(8);
    app.reset();
    capturedHandler(QtDebugMsg, QMessageLogContext(), QStringLiteral("late callback"));
    running = false;
    for (auto &thread : threads)
        thread.join();

    if (destroyedAppenders != 1 || appendedMessages < 8 || fallbackMessages == 0) {
        std::fprintf(stderr, "destroyed=%d appended=%d fallback=%d\n", destroyedAppenders.load(),
                     appendedMessages.load(), fallbackMessages.load());
        return 2;
    }
    if (qInstallMessageHandler(fallbackHandler) != (chained ? forwardingHandler : fallbackHandler))
        return 3;
    return 0;
}
