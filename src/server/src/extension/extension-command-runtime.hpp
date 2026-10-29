#pragma once
#include "command/command.hpp"
#include "command/command-types.hpp"
#include "extension/extension-command.hpp"
#include "extension/extension-api-server.hpp"
#include "extension/manager/extension-manager.hpp"
#include "generated/tsapi.hpp"
#include <qlogging.h>

class ExtensionCommandRuntime : public CommandContext {
  void initialize();

public:
  void load(const LaunchProps &props) override;
  void unload() override;

  ExtensionCommandRuntime(const std::shared_ptr<ExtensionCommand> &command);

private:
  std::unique_ptr<ExtensionManagerBus> m_bus;
  std::unique_ptr<ExtensionLogger> m_logger;
  std::unique_ptr<tsapi::RpcTransport> m_transport;
  std::shared_ptr<ExtensionCommand> m_command;
  tsapi::Server *m_server = nullptr;

  std::string m_sessionId;
  bool m_isDevMode = false;
  bool m_headless = false;

public:
  void setHeadless(bool value) { m_headless = value; }
};
