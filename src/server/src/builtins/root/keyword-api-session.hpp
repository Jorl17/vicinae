#pragma once
#include "extension/extension-api-server.hpp"
#include <QObject>
#include <memory>
#include <string>

struct ApplicationContext;
class ExtensionCommand;

/* The session in which the keyword handler of an extension command calls the
 * Vicinae API, for example getFrontmostApplication, as a loaded command does.
 * We pass its id with each keyword query of the command (see queryKeyword in
 * manager.fig), and answer the calls that come back with that id. */
class KeywordApiSession : public QObject {
public:
  KeywordApiSession(std::shared_ptr<ExtensionCommand> command, ApplicationContext &ctx,
                    QObject *parent = nullptr);

  const std::string &id() const { return m_id; }
  const std::shared_ptr<ExtensionCommand> &command() const { return m_command; }

private:
  std::string m_id;
  std::shared_ptr<ExtensionCommand> m_command;
  ExtensionManagerBus m_bus;
  ExtensionLogger m_logger;
  tsapi::RpcTransport m_transport;
  tsapi::Server *m_server = nullptr;
};
