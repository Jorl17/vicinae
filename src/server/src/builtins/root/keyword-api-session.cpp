#include "builtins/root/keyword-api-session.hpp"
#include "common/context.hpp"
#include "service-registry.hpp"
#include <QUuid>

KeywordApiSession::KeywordApiSession(std::shared_ptr<ExtensionCommand> command, ApplicationContext &ctx,
                                     QObject *parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()),
      m_command(std::move(command)), m_bus(*ctx.services->extensionManager()), m_transport(m_bus) {
  m_bus.setSessionId(m_id);
  m_server = createExtensionApiServer(m_transport, m_command, ctx, this);
  m_server->setLogger(&m_logger);

  connect(ctx.services->extensionManager(), &ExtensionManager::extensionMessageReceived, this,
          [this](const std::string &sessionId, std::string_view data) {
            if (sessionId == m_id) m_server->route(data);
          });
}
