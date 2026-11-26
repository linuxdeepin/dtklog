// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef DTKLOG_FILEAPPENDER_P_H
#define DTKLOG_FILEAPPENDER_P_H

#include "rollingfilesink_p.h"

DLOG_CORE_BEGIN_NAMESPACE
namespace detail {
std::shared_ptr<spdlog::logger> fileLogger(const FileAppender *appender);
void setFileSizeLimit(const FileAppender *appender, std::size_t limit);
void setFileCountLimit(const FileAppender *appender, std::size_t limit);
void setFileInterval(const FileAppender *appender, RollingInterval interval);
}
DLOG_CORE_END_NAMESPACE

#endif
