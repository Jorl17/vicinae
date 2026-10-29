#pragma once
#include "extension/extension-command.hpp"
#include "extension/manager/extension-manager.hpp"
#include "generated/tsapi.hpp"
#include <QObject>
#include <memory>
#include <qlogging.h>
#include <string>
#include <string_view>

struct ApplicationContext;

class ExtensionManagerBus : public tsapi::AbstractTransport {
public:
  ExtensionManagerBus(ExtensionManager &manager) : m_manager(manager) {}

  void send(std::string_view data) override {
    m_manager.client().manager()->messageExtension(m_sessionId, std::string{data});
  }

  void setSessionId(std::string str) { m_sessionId = std::move(str); }

private:
  ExtensionManager &m_manager;
  std::string m_sessionId;
};

class ExtensionLogger : public tsapi::AbstractLogger {
  void onRequest(std::string_view method) override { qDebug() << "-->" << method; }
  void onResponse(std::string_view method, bool ok, double latencyMs) override {
    qDebug() << "<-- " << method << latencyMs << "ms" << (ok ? "OK" : "ERROR");
  }
  void onEvent(std::string_view method) override { qDebug() << "--> " << method; }
};

/* The server that answers the Vicinae API calls of an extension command, such as
 * getFrontmostApplication, over the transport. Loaded commands and keyword
 * handlers use the same services. The server is a child of parent. */
tsapi::Server *createExtensionApiServer(tsapi::RpcTransport &transport,
                                        const std::shared_ptr<ExtensionCommand> &command,
                                        ApplicationContext &ctx, QObject *parent);
