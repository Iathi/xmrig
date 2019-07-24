/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2016-2017 XMRig       <support@xmrig.com>
 * Copyright 2017-     BenDr0id    <ben@graef.in>
 *
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

#include <chrono>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iostream>

#ifdef WIN32
#include "win_dirent.h"
#endif

#include "log/Log.h"
#include <3rdparty/cpp-httplib/httplib.h>
#include <3rdparty/rapidjson/document.h>
#include <3rdparty/rapidjson/stringbuffer.h>
#include <3rdparty/rapidjson/writer.h>
#include <3rdparty/rapidjson/filewritestream.h>
#include <3rdparty/rapidjson/filereadstream.h>
#include <3rdparty/rapidjson/error/en.h>
#include <3rdparty/rapidjson/prettywriter.h>
#include "version.h"
#include "Service.h"

uv_mutex_t Service::m_mutex;
uv_timer_t Service::m_timer;

std::map<std::string, ControlCommand> Service::m_clientCommand;
std::map<std::string, ClientStatus> Service::m_clientStatus;
std::map<std::string, std::list<std::string>> Service::m_clientLog;

std::list<std::string> Service::m_offlineNotified;
std::list<std::string> Service::m_zeroHashNotified;

uint64_t Service::m_currentServerTime = 0;
uint64_t Service::m_lastStatusUpdateTime = 0;

bool Service::start()
{
    uv_mutex_init(&m_mutex);

#ifndef XMRIG_NO_TLS
    if (Options::i()->ccUsePushover() || Options::i()->ccUseTelegram())
    {
        uv_timer_init(uv_default_loop(), &m_timer);
        uv_timer_start(&m_timer, Service::onPushTimer,
                       static_cast<uint64_t>(TIMER_INTERVAL),
                       static_cast<uint64_t>(TIMER_INTERVAL));
    }
#endif

    return true;
}

void Service::release()
{
    uv_mutex_lock(&m_mutex);

    uv_timer_stop(&m_timer);

    m_clientCommand.clear();
    m_clientStatus.clear();
    m_clientLog.clear();

    uv_mutex_unlock(&m_mutex);
}

unsigned Service::handleGET(const Options* options, const std::string& url, const std::string& clientIp, const std::string& clientId, std::string& resp)
{
    uv_mutex_lock(&m_mutex);

    unsigned resultCode = MHD_HTTP_NOT_FOUND;

    std::string params;
    if (!clientId.empty())
    {
        params += "?clientId=";
        params += clientId;
    }

    LOG_INFO("[%s] GET '%s%s'", clientIp.c_str(), url.c_str(), params.c_str());

    if (url == "/") {
        resultCode = getAdminPage(options, resp);
    } else if (url.rfind("/admin/getClientStatusList", 0) == 0) {
        resultCode = getClientStatusList(resp);
    } else if (url.rfind("/admin/getClientConfigTemplates", 0) == 0) {
        resultCode = getClientConfigTemplates(options, resp);
    } else {
        if (!clientId.empty()) {
            if (url.rfind("/client/getConfig", 0) == 0 || url.rfind("/admin/getClientConfig", 0) == 0) {
                resultCode = getClientConfig(options, clientId, resp);
            } else if (url.rfind("/admin/getClientCommand", 0) == 0) {
                resultCode = getClientCommand(clientId, resp);
            } else if (url.rfind("/admin/getClientLog", 0) == 0) {
                resultCode = getClientLog(clientId, resp);
            } else {
                LOG_WARN("[%s] 404 NOT FOUND (%s)", clientIp.c_str(), url.c_str());
            }
        }
        else {
            resultCode = MHD_HTTP_BAD_REQUEST;
            LOG_ERR("[%s] 400 BAD REQUEST - Request does not contain clientId (%s)", clientIp.c_str(), url.c_str());
        }
    }

    uv_mutex_unlock(&m_mutex);

    return resultCode;
}

unsigned Service::handlePOST(const Options* options, const std::string& url, const std::string& clientIp,
                             const std::string& clientId, const std::string& data, std::string& resp)
{
    uv_mutex_lock(&m_mutex);

    unsigned resultCode = MHD_HTTP_NOT_FOUND;

    std::string params;
    if (!clientId.empty())
    {
        params += "?clientId=";
        params += clientId;
    }

    LOG_INFO("[%s] POST '%s%s', dataLen='%d'",
             clientIp.c_str(), url.c_str(), params.c_str(), data.length());

    if (!clientId.empty()) {
        if (url.rfind("/client/setClientStatus", 0) == 0) {
            resultCode = setClientStatus(options, clientIp, clientId, data, resp);
        } else if (url.rfind("/admin/setClientConfig", 0) == 0 || url.rfind("/client/setClientConfig", 0) == 0) {
            resultCode = setClientConfig(options, clientId, data, resp);
        } else if (url.rfind("/admin/setClientCommand", 0) == 0) {
            resultCode = setClientCommand(clientId, data, resp);
        } else if (url.rfind("/admin/deleteClientConfig", 0) == 0) {
            resultCode = deleteClientConfig(options, clientId, resp);
        } else {
            LOG_WARN("[%s] 400 BAD REQUEST - Request does not contain clientId (%s)", clientIp.c_str(), url.c_str());
        }
    } else {
        if (url.rfind("/admin/resetClientStatusList", 0) == 0) {
            resultCode = resetClientStatusList(data, resp);
        } else {
            LOG_WARN("[%s] 404 NOT FOUND (%s)", clientIp.c_str(), url.c_str());
        }
    }

    uv_mutex_unlock(&m_mutex);

    return resultCode;
}

unsigned Service::getClientConfig(const Options* options, const std::string& clientId, std::string& resp)
{
    unsigned resultCode = MHD_HTTP_INTERNAL_SERVER_ERROR;

    std::string clientConfigFileName = getClientConfigFileName(options, clientId);

    std::stringstream data;
    std::ifstream clientConfig(clientConfigFileName);
    if (clientConfig) {
        data << clientConfig.rdbuf();
        clientConfig.close();
    } else {
        std::ifstream defaultConfig("default_miner_config.json");
        if (defaultConfig) {
            data << defaultConfig.rdbuf();
            defaultConfig.close();
        }
    }

    if (data.tellp() > 0) {
        rapidjson::Document document;
        document.Parse(data.str().c_str());

        if (!document.HasParseError()) {
            rapidjson::StringBuffer buffer(0, 4096);
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            writer.SetMaxDecimalPlaces(10);
            document.Accept(writer);

            resp = buffer.GetString();

            resultCode = MHD_HTTP_OK;
        } else {
            LOG_ERR("Not able to send client config. Client config %s is broken!", clientConfigFileName.c_str());
        }
    } else{
        LOG_ERR("Not able to load a client config. Please check your configuration!");
    }

    return resultCode;
}

unsigned Service::setClientConfig(const Options* options, const std::string &clientId, const std::string &data, std::string &resp)
{
    unsigned resultCode = MHD_HTTP_BAD_REQUEST;

    rapidjson::Document document;
    if (!document.Parse(data.c_str()).HasParseError()) {
        std::string clientConfigFileName = getClientConfigFileName(options, clientId);
        std::ofstream clientConfigFile(clientConfigFileName);

        if (clientConfigFile){
            rapidjson::StringBuffer buffer(0, 4096);
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            writer.SetMaxDecimalPlaces(10);
            document.Accept(writer);

            clientConfigFile << buffer.GetString();
            clientConfigFile.close();

            resultCode = MHD_HTTP_OK;
        } else {
            LOG_ERR("Not able to store client config to file %s.", clientConfigFileName.c_str());
        }
    } else{
        LOG_ERR("Not able to store client config. The received client config for client %s is broken!", clientId.c_str());
    }

    return resultCode;
}

unsigned Service::getClientStatusList(std::string& resp)
{
    rapidjson::Document document;
    document.SetObject();

    auto& allocator = document.GetAllocator();

    rapidjson::Value clientStatusList(rapidjson::kArrayType);
    for (auto& clientStatus : m_clientStatus) {
        rapidjson::Value clientStatusEntry(rapidjson::kObjectType);
        clientStatusEntry.AddMember("client_status", clientStatus.second.toJson(allocator), allocator);
        clientStatusList.PushBack(clientStatusEntry, allocator);
    }

    auto time_point = std::chrono::system_clock::now();
    m_currentServerTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(time_point));

    document.AddMember("current_server_time", m_currentServerTime, allocator);
    document.AddMember("current_version", rapidjson::StringRef(Version::string().c_str()), allocator);
    document.AddMember("client_status_list", clientStatusList, allocator);

    rapidjson::StringBuffer buffer(0, 4096);
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.SetMaxDecimalPlaces(10);
    document.Accept(writer);

    resp = buffer.GetString();

    return MHD_HTTP_OK;
}

unsigned Service::setClientStatus(const Options* options, const std::string& clientIp, const std::string& clientId, const std::string& data, std::string& resp)
{
    unsigned resultCode = MHD_HTTP_BAD_REQUEST;

    rapidjson::Document document;
    if (!document.Parse(data.c_str()).HasParseError()) {
        ClientStatus clientStatus;
        clientStatus.parseFromJson(document);
        clientStatus.setExternalIp(clientIp);

        setClientLog(options->ccClientLogLinesHistory(), clientId, clientStatus.getLog());

        clientStatus.clearLog();

        m_clientStatus[clientId] = clientStatus;

        resultCode = getClientCommand(clientId, resp);

        if (m_clientCommand[clientId].isOneTimeCommand()) {
            m_clientCommand.erase(clientId);
        }
    } else {
        LOG_ERR("[%s] ClientStatus for client '%s' - Parse Error Occured: %d",
                clientIp.c_str(), clientId.c_str(), document.GetParseError());
    }

    return resultCode;
}

unsigned Service::getClientCommand(const std::string& clientId, std::string& resp)
{
    if (m_clientCommand.find(clientId) == m_clientCommand.end()) {
        m_clientCommand[clientId] = ControlCommand();
    }

    rapidjson::Document respDocument;
    respDocument.SetObject();

    auto& allocator = respDocument.GetAllocator();

    rapidjson::Value controlCommand = m_clientCommand[clientId].toJson(allocator);
    respDocument.AddMember("control_command", controlCommand, allocator);

    rapidjson::StringBuffer buffer(0, 4096);
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.SetMaxDecimalPlaces(10);
    respDocument.Accept(writer);

    resp = buffer.GetString();

    return MHD_HTTP_OK;
}

unsigned Service::getClientLog(const std::string& clientId, std::string& resp)
{
    if (m_clientLog.find(clientId) != m_clientLog.end()) {
        rapidjson::Document respDocument;
        respDocument.SetObject();

        auto& allocator = respDocument.GetAllocator();

        std::stringstream data;
        for (auto& m_row : m_clientLog[clientId]) {
            data << m_row.c_str() << std::endl;
        }

        std::string log = data.str();
        respDocument.AddMember("client_log", rapidjson::StringRef(log.c_str()), allocator);

        rapidjson::StringBuffer buffer(0, 4096);
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.SetMaxDecimalPlaces(10);
        respDocument.Accept(writer);

        resp = buffer.GetString();
    }

    return MHD_HTTP_OK;
}

unsigned Service::getClientConfigTemplates(const Options* options, std::string& resp)
{
    std::string configFolder(".");

    if (options->ccClientConfigFolder() != nullptr) {
        configFolder = options->ccClientConfigFolder();
#       ifdef WIN32
        configFolder += '\\';
#       else
        configFolder += '/';
#       endif
    }

    std::vector<std::string> templateFiles;

    DIR* dirp = opendir(configFolder.c_str());
    if (dirp) {
        struct dirent* entry;
        while ((entry = readdir(dirp)) != NULL) {
            if (entry->d_type == DT_REG) {
                std::string filename = entry->d_name;
                std::string starting = "template_";
                std::string ending = "_config.json";

                if (filename.rfind(starting, 0) == 0 && filename.find(ending, (filename.length() - ending.length())) != std::string::npos) {
                    filename.erase(0, starting.length());
                    filename.erase(filename.length()-ending.length());

                    templateFiles.push_back(filename);
                }
            }
        }

        closedir(dirp);
    }

    rapidjson::Document respDocument;
    respDocument.SetObject();

    auto& allocator = respDocument.GetAllocator();

    rapidjson::Value templateList(rapidjson::kArrayType);
    for (auto& templateFile : templateFiles) {
        rapidjson::Value templateEntry(templateFile.c_str(), allocator);
        templateList.PushBack(templateEntry, allocator);
    }

    respDocument.AddMember("templates", templateList, allocator);

    rapidjson::StringBuffer buffer(0, 4096);
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.SetMaxDecimalPlaces(10);
    respDocument.Accept(writer);

    resp = buffer.GetString();

    return MHD_HTTP_OK;
}

unsigned Service::getAdminPage(const Options* options, std::string& resp)
{
    std::stringstream data;

    if (options->ccCustomDashboard() != nullptr) {
        std::ifstream customDashboard(options->ccCustomDashboard());
        if (customDashboard)
        {
            data << customDashboard.rdbuf();
            customDashboard.close();
            resp = data.str();
        }
    }

    if (resp.empty()) {
        data << "<!DOCTYPE html>";
        data << "<html lang=\"en\">";
        data << "<head>";
        data << "<meta charset=\"utf-8\">";
        data << "<title>XMRigCC Dashboard</title>";
        data << "</head>";
        data << "<body>";
        data << "    <div style=\"text-align: center;\">";
        data << "       <h1>Please configure a Dashboard</h1>";
        data << "    </div>";
        data << "</body>";
        data << "</html>";
    }

    resp = data.str();

    return MHD_HTTP_OK;
}

unsigned Service::setClientCommand(const std::string& clientId, const std::string& data, std::string& resp)
{
    ControlCommand controlCommand;

    rapidjson::Document document;
    if (!document.Parse(data.c_str()).HasParseError()) {
        controlCommand.parseFromJson(document);

        m_clientCommand[clientId] = controlCommand;

        return MHD_HTTP_OK;
    } else {
        return MHD_HTTP_BAD_REQUEST;
    }
}

void Service::setClientLog(size_t maxRows, const std::string& clientId, const std::string& log)
{
    if (m_clientLog.find(clientId) == m_clientLog.end()) {
        m_clientLog[clientId] = std::list<std::string>();
    }

    auto* clientLog = &m_clientLog[clientId];
    std::istringstream logStream(log);

    std::string logLine;
    while (std::getline(logStream, logLine))
    {
        if (clientLog->size() == maxRows) {
            clientLog->pop_front();
        }

        clientLog->push_back(logLine);
    }
}

unsigned Service::deleteClientConfig(const Options* options, const std::string& clientId, std::string& resp)
{
    if (!clientId.empty()) {
        std::string clientConfigFileName = getClientConfigFileName(options, clientId);
        if (!clientConfigFileName.empty() && remove(clientConfigFileName.c_str()) == 0) {
            return MHD_HTTP_OK;
        } else {
            return MHD_HTTP_NOT_FOUND;
        }
    } else {
        return MHD_HTTP_BAD_REQUEST;
    }
}

unsigned Service::resetClientStatusList(const std::string& data, std::string& resp)
{
    m_clientStatus.clear();

    return MHD_HTTP_OK;
}

std::string Service::getClientConfigFileName(const Options* options, const std::string& clientId)
{
    std::string clientConfigFileName;

    if (options->ccClientConfigFolder() != nullptr) {
        clientConfigFileName += options->ccClientConfigFolder();
#       ifdef WIN32
        clientConfigFileName += '\\';
#       else
        clientConfigFileName += '/';
#       endif
    }

    clientConfigFileName += clientId + std::string("_config.json");

    return clientConfigFileName;
}

void Service::onPushTimer(uv_timer_t* handle)
{
    auto time_point = std::chrono::system_clock::now();
    auto now = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(time_point) * 1000);

    if (Options::i()->ccPushOfflineMiners()) {
        sendMinerOfflinePush(now);
    }

    if (Options::i()->ccPushZeroHashrateMiners()) {
        sendMinerZeroHashratePush(now);
    }

    if (Options::i()->ccPushPeriodicStatus()) {
        if (now > (m_lastStatusUpdateTime + STATUS_UPDATE_INTERVAL)) {
            sendServerStatusPush(now);
            m_lastStatusUpdateTime = now;
        }
    }
}

void Service::sendMinerOfflinePush(uint64_t now)
{
    uint64_t offlineThreshold = now - OFFLINE_TRESHOLD_IN_MS;

    for (auto clientStatus : m_clientStatus) {
        uint64_t lastStatus = clientStatus.second.getLastStatusUpdate() * 1000;

        if (lastStatus < offlineThreshold) {
            if (std::find(m_offlineNotified.begin(), m_offlineNotified.end(), clientStatus.first) == m_offlineNotified.end()) {
                std::stringstream message;
                message << "Miner: " << clientStatus.first << " just went offline!";

                LOG_WARN("Send miner %s went offline push", clientStatus.first.c_str());
                triggerPush(APP_NAME " Onlinestatus Monitor", message.str());

                m_offlineNotified.push_back(clientStatus.first);
            }
        } else {
            if (std::find(m_offlineNotified.begin(), m_offlineNotified.end(), clientStatus.first) != m_offlineNotified.end()) {
                std::stringstream message;
                message << "Miner: " << clientStatus.first << " is back online!";

                LOG_WARN("Send miner %s back online push", clientStatus.first.c_str());
                triggerPush(APP_NAME " Onlinestatus Monitor", message.str());

                m_offlineNotified.remove(clientStatus.first);
            }
        }
    }
}

void Service::sendMinerZeroHashratePush(uint64_t now)
{
    uint64_t offlineThreshold = now - OFFLINE_TRESHOLD_IN_MS;

    for (auto clientStatus : m_clientStatus) {
        if (offlineThreshold < clientStatus.second.getLastStatusUpdate() * 1000) {
            if (clientStatus.second.getHashrateShort() == 0 && clientStatus.second.getHashrateMedium() == 0) {
                if (std::find(m_zeroHashNotified.begin(), m_zeroHashNotified.end(), clientStatus.first) == m_zeroHashNotified.end()) {
                    std::stringstream message;
                    message << "Miner: " << clientStatus.first << " reported 0 h/s for the last minute!";

                    LOG_WARN("Send miner %s 0 hashrate push", clientStatus.first.c_str());
                    triggerPush(APP_NAME " Hashrate Monitor", message.str());

                    m_zeroHashNotified.push_back(clientStatus.first);
                }
            } else if (clientStatus.second.getHashrateMedium() > 0) {
                if (std::find(m_zeroHashNotified.begin(), m_zeroHashNotified.end(), clientStatus.first) != m_zeroHashNotified.end()) {
                    std::stringstream message;
                    message << "Miner: " << clientStatus.first << " hashrate recovered. Reported "
                            << clientStatus.second.getHashrateMedium()
                            << " h/s for the last minute!";

                    LOG_WARN("Send miner %s hashrate recovered push", clientStatus.first.c_str());
                    triggerPush(APP_NAME " Hashrate Monitor", message.str());

                    m_zeroHashNotified.remove(clientStatus.first);
                }
            }
        }
    }
}

void Service::sendServerStatusPush(uint64_t now)
{
    size_t onlineMiner = 0;
    size_t offlineMiner = 0;

    double hashrateMedium = 0;
    double hashrateLong = 0;
    double avgTime = 0;

    uint64_t sharesGood = 0;
    uint64_t sharesTotal = 0;
    uint64_t offlineThreshold = now - OFFLINE_TRESHOLD_IN_MS;

    for (auto clientStatus : m_clientStatus) {
        if (offlineThreshold < clientStatus.second.getLastStatusUpdate() * 1000) {
            onlineMiner++;

            hashrateMedium += clientStatus.second.getHashrateMedium();
            hashrateLong += clientStatus.second.getHashrateLong();

            sharesGood += clientStatus.second.getSharesGood();
            sharesTotal += clientStatus.second.getSharesTotal();
            avgTime += clientStatus.second.getAvgTime();
        } else {
            offlineMiner++;
        }
    }

    if (!m_clientStatus.empty()) {
        avgTime = avgTime / m_clientStatus.size();
    }

    std::stringstream message;
    message << "Miners: " << onlineMiner << " (Online), " << offlineMiner << " (Offline)\n"
            << "Shares: " << sharesGood << " (Good), " << sharesTotal - sharesGood << " (Bad)\n"
            << "Hashrates: " << hashrateMedium << "h/s (1min), " << hashrateLong << "h/s (15min)\n"
            << "Avg. Time: " << avgTime << "s";

    LOG_WARN("Send Server status push");
    triggerPush(APP_NAME " Status", message.str());
}

void Service::triggerPush(const std::string& title, const std::string& message)
{
    if (Options::i()->ccUsePushover()) {
        sendViaPushover(title, message);
    }

    if (Options::i()->ccUseTelegram()) {
        sendViaTelegram(title, message);
    }
}

void Service::sendViaPushover(const std::string &title, const std::string &message)
{
    std::shared_ptr<httplib::Client> cli = std::make_shared<httplib::SSLClient>("api.pushover.net", 443);

    httplib::Params params;
    params.emplace("token", Options::i()->ccPushoverToken());
    params.emplace("user", Options::i()->ccPushoverUser());
    params.emplace("title", title);
    params.emplace("message", httplib::detail::encode_url(message));

    auto res = cli->Post("/1/messages.json", params);
    if (res) {
        LOG_WARN("Pushover response: %s", res->body.c_str());
    }
}

void Service::sendViaTelegram(const std::string &title, const std::string &message)
{
    std::shared_ptr<httplib::Client> cli = std::make_shared<httplib::SSLClient>("api.telegram.org", 443);

    std::string text = "<b>" + title + "</b>\n\n" + message;
    std::string path = std::string("/bot") + Options::i()->ccTelegramBotToken() + std::string("/sendMessage");

    httplib::Params params;
    params.emplace("chat_id", Options::i()->ccTelegramChatId());
    params.emplace("text", httplib::detail::encode_url(text));
    params.emplace("parse_mode", "HTML");

    auto res = cli->Post(path.c_str(), params);
    if (res) {
        LOG_WARN("Telegram response: %s", res->body.c_str());
    }
}
