#include "extension/extension-api-server.hpp"
#include "common/context.hpp"
#include "extension/api/application-service.hpp"
#include "extension/api/clipboard-service.hpp"
#include "extension/api/command-service.hpp"
#include "extension/api/event-core-service.hpp"
#include "extension/api/ext-browser-extension-service.hpp"
#include "extension/api/file-search-service.hpp"
#include "extension/api/oauth-service.hpp"
#include "extension/api/storage-service.hpp"
#include "extension/api/ui-service.hpp"
#include "extension/api/wallpaper-service.hpp"
#include "extension/api/wm-service.hpp"
#include "service-registry.hpp"
#include "services/app-service/app-service.hpp"
#include "services/root-item-manager/root-item-manager.hpp"
#include "services/toast/toast.hpp"

tsapi::Server *createExtensionApiServer(tsapi::RpcTransport &transport,
                                        const std::shared_ptr<ExtensionCommand> &command,
                                        ApplicationContext &ctx, QObject *parent) {
  QString const storageNamespace = QString("%1:data").arg(command->uniqueId().provider.c_str());
  auto services = ctx.services;

  auto *eventCore = new ExtEventCoreService(transport);
  auto *app = new ExtApplicationService(transport, *services->appDb());
  auto *ui = new ExtUIService(transport, ctx.navigation.get(), command, eventCore, *services->toastService(),
                              *services->selectionService());
  auto *wm = new ExtWindowManagementService(transport, *services->windowManager(), *services->appDb(),
                                            *services->appRuntime(), *ctx.navigation);
  auto *clipboard = new ExtClipboardService(transport, *services->clipman(), *services->pasteService());
  auto *storage = new ExtStorageService(transport, *services->localStorage(), storageNamespace);
  auto *fileSearch = new ExtFileSearchService(transport, *services->fileService());
  auto *commandService =
      new ExtCommandService(transport, command, services->rootItemManager(), *ctx.settings, *ctx.navigation);
  auto *oauth = new ExtOAuthService(transport, command->extensionId(), ctx);
  auto *wallpaper = new ExtWallpaperService(transport, *services->wallpaperManager());
  auto *browserExtension = new ExtBrowserExtensionService(transport, *services->browserExtension());

  auto *server = new tsapi::Server(transport, app, ui, wm, clipboard, storage, fileSearch, commandService,
                                   oauth, wallpaper, browserExtension, eventCore);
  server->setParent(parent);
  return server;
}
