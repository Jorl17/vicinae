#include "extension-command-runtime.hpp"
#include "extension/extension-load-options.hpp"
#include "command/command-types.hpp"
#include "common/context.hpp"
#include "extension/views/extension-error-view-host.hpp"
#include "extension/api/application-service.hpp"
#include "extension/api/ext-browser-extension-service.hpp"
#include "extension/api/clipboard-service.hpp"
#include "extension/api/command-service.hpp"
#include "extension/api/event-core-service.hpp"
#include "extension/api/file-search-service.hpp"
#include "extension/api/oauth-service.hpp"
#include "extension/api/storage-service.hpp"
#include "extension/api/ui-service.hpp"
#include "extension/api/wm-service.hpp"
#include "extension/api/wallpaper-service.hpp"
#include "generated/tsapi.hpp"
#include "glaze-qt.hpp"
#include "service-registry.hpp"
#include "services/app-service/app-service.hpp"
#include "services/asset-resolver/asset-resolver.hpp"
#include <QString>
#include <glaze/json/generic.hpp>
#include <glaze/json/prettify.hpp>
#include <glaze/json/write.hpp>
#include <qfuturewatcher.h>
#include <qlogging.h>
#include <ranges>
#include "services/root-item-manager/root-item-manager.hpp"
#include "extension/manager/extension-manager.hpp"
#include "services/toast/toast.hpp"
#include "vicinae.hpp"
#include "generated/manager.hpp"

void ExtensionCommandRuntime::initialize() {
  auto manager = context()->services->extensionManager();

  m_bus = std::make_unique<ExtensionManagerBus>(*manager);
  m_logger = std::make_unique<ExtensionLogger>();
  m_transport = std::make_unique<tsapi::RpcTransport>(*m_bus);

  RelativeAssetResolver::instance()->addPath(m_command->assetPath());

  m_isDevMode = manager->hasDevelopmentSession(m_command->extensionId());

  QString const storageNamespace = QString("%1:data").arg(m_command->uniqueId().provider.c_str());

  auto services = context()->services;
  auto &ctx = *context();

  auto *eventCore = new ExtEventCoreService(*m_transport);
  auto *app = new ExtApplicationService(*m_transport, *services->appDb());
  auto *ui = new ExtUIService(*m_transport, context()->navigation.get(), m_command, eventCore,
                              *services->toastService(), *services->selectionService());
  auto *wm = new ExtWindowManagementService(*m_transport, *services->windowManager(), *services->appDb(),
                                            *services->appRuntime(), *ctx.navigation);
  auto *clipboard = new ExtClipboardService(*m_transport, *services->clipman(), *services->pasteService());
  auto *storage = new ExtStorageService(*m_transport, *services->localStorage(), storageNamespace);
  auto *fileSearch = new ExtFileSearchService(*m_transport, *services->fileService());
  auto *command = new ExtCommandService(*m_transport, m_command, services->rootItemManager(), *ctx.settings,
                                        *ctx.navigation);
  auto *oauth = new ExtOAuthService(*m_transport, m_command->extensionId(), ctx);
  auto wallpaper = new ExtWallpaperService(*m_transport, *services->wallpaperManager());
  auto browserExtension = new ExtBrowserExtensionService(*m_transport, *services->browserExtension());

  m_server = new tsapi::Server(*m_transport, app, ui, wm, clipboard, storage, fileSearch, command, oauth,
                               wallpaper, browserExtension, eventCore);
  m_server->setLogger(m_logger.get());
  m_server->setParent(this);
}

void ExtensionCommandRuntime::load(const LaunchProps &props) {
  initialize();

  auto manager = context()->services->extensionManager();
  auto opts = extensionLoadOptions(*m_command, *context()->services);

  if (m_command->mode() == CommandModeView) {
    // We push the first view immediately, waiting for the initial render to come
    // in and "hydrate" it.
    m_server->UI()->pushView();
  }

  if (m_isDevMode) {
    if (!m_headless)
      context()->navigation->setNavigationSuffixIcon(
          ImageURL::builtin(BuiltinIcon::Hammer).setFill(SemanticColor::Green));
    opts.env = manager::CommandEnv::Development;
  }

  opts.cwd = props.cwd.transform([](const QString &s) { return s.toStdString(); });
  opts.fallbackText = props.fallbackText.transform([](const QString &s) { return s.toStdString(); });
  opts.launch_context = props.launchContext;
  opts.arguments = props.arguments |
                   std::views::transform([](auto &&pair) -> std::pair<std::string, std::string> {
                     return {pair.first.toStdString(), pair.second.toStdString()};
                   }) |
                   std::ranges::to<std::unordered_map<std::string, std::string>>();

  // FIXME: relying on the presence of props.cwd to infer CommandLine mode is not
  // very accurate and could break in the future, we probably want to pass the activation
  // mode in the props at some point and map them to the figura types.
  if (props.cwd) {
    opts.launch_type = manager::LaunchType::CommandLine;
  } else if (m_headless) {
    opts.launch_type = manager::LaunchType::Background;
  }

  auto watcher = new QFutureWatcher<std::expected<manager::LoadResponse, std::string>>(this);

  connect(manager, &ExtensionManager::extensionMessageReceived, this,
          [this](const std::string &sessionId, std::string_view data) {
            if (sessionId != m_sessionId) return; // not for us
            m_server->route(data);
          });

  connect(manager, &ExtensionManager::extensionCrashed, this,
          [this](const std::string &sessionId, const std::string &reason) {
            if (sessionId != m_sessionId) return;

            qCritical() << "Got crash" << reason;

            if (m_headless) return;

            auto &nav = context()->navigation;

            nav->popToRoot();
            nav->pushView(new ExtensionErrorViewHost(QString::fromStdString(reason)));
            nav->setNavigationTitle(QString("%1 - Crash handler").arg(m_command->name()));
            nav->setNavigationIcon(m_command->iconUrl());
          });

  connect(watcher, &QFutureWatcherBase::finished, this, [this, manager, watcher]() {
    if (!watcher->isCanceled()) {
      auto res = watcher->result();

      if (!res) {
        qWarning() << "Failed to load extension" << res.error();
      } else {
        m_sessionId = res->session_id;
        m_bus->setSessionId(m_sessionId);
        manager->client().manager()->ready(res->session_id);
      }
    }

    watcher->deleteLater();
  });

  watcher->setFuture(manager->client().manager()->load(opts));
}

void ExtensionCommandRuntime::unload() {
  RelativeAssetResolver::instance()->removePath(m_command->assetPath());
  auto manager = context()->services->extensionManager();
  auto toast = context()->services->toastService();

  // make sure we are not carrying any loading state outside of the command
  // it's fine to let other toasts dismiss themselves automatically after the default timeout.
  if (auto const current = toast->currentToast()) {
    if (current->priority() == ToastStyle::Dynamic) toast->clear();
  }

  manager->client().manager()->unload(m_sessionId);

  if (!m_headless) context()->navigation->setNavigationSuffixIcon(std::nullopt);
}

ExtensionCommandRuntime::ExtensionCommandRuntime(const std::shared_ptr<ExtensionCommand> &command)
    : CommandContext(command), m_command(command) {}
