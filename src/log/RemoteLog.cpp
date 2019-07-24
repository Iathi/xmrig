/* XMRig
 * Copyright 2018-     BenDr0id <ben@graef.in>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <sstream>
#include <regex>
#include "log/RemoteLog.h"

RemoteLog* RemoteLog::m_self = nullptr;
static const std::regex COLOR_PATTERN("\x1B\\[[0-9;]*[a-zA-Z]");

RemoteLog::RemoteLog(size_t maxRows)
    : m_maxRows(maxRows)
{
    uv_mutex_init(&m_mutex);

    m_self = this;
}

RemoteLog::~RemoteLog()
{
    m_self = nullptr;

    uv_mutex_destroy(&m_mutex);
}

void RemoteLog::message(int level, const char* fmt, va_list args)
{
    time_t now = time(nullptr);
    tm stime;

#   ifdef _WIN32
    localtime_s(&stime, &now);
#   else
    localtime_r(&now, &stime);
#   endif

    auto *buf = new char[512];
    int size = snprintf(buf, 23, "[%d-%02d-%02d %02d:%02d:%02d] ",
                        stime.tm_year + 1900,
                        stime.tm_mon + 1,
                        stime.tm_mday,
                        stime.tm_hour,
                        stime.tm_min,
                        stime.tm_sec);

    size = vsnprintf(buf + size, static_cast<size_t>(512 - size - 1), fmt, args) + size;
    buf[size] = '\n';

    std::string coloredLogLine(buf, static_cast<unsigned long>(size + 1));
    std::string logLine = std::regex_replace(coloredLogLine, COLOR_PATTERN, "");

    uv_mutex_lock(&m_mutex);

    if (m_rows.size() == m_maxRows) {
        m_rows.pop_front();
    }

    m_rows.push_back(logLine);

    uv_mutex_unlock(&m_mutex);

    delete[](buf);
}


void RemoteLog::text(const char* fmt, va_list args)
{
    message(0, fmt, args);
}

std::string RemoteLog::getRows()
{
    std::stringstream data;

    if (m_self) {
        uv_mutex_lock(&m_self->m_mutex);

        for (auto& m_row : m_self->m_rows) {
            data << m_row.c_str();
        }

        m_self->m_rows.clear();

        uv_mutex_unlock(&m_self->m_mutex);
    }

    return data.str();
}
