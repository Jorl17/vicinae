#include "extension/extension-load-options.hpp"
#include <glaze/json/generic.hpp>
#include "extension/extension-command.hpp"
#include "service-registry.hpp"
#include "services/app-service/app-service.hpp"
#include "services/browser-extension-service.hpp"
#include "services/files-service/file-service.hpp"
#include "services/root-item-manager/root-item-manager.hpp"
#include "services/wallpaper/wallpaper-manager.hpp"
#include "services/window-manager/window-manager.hpp"
#include "vicinae.hpp"

namespace {

// app pickers are stored as app ids, but extensions expect the `Application` shape of the applications API
PreferenceValues resolvePreferenceValues(const ExtensionCommand &command, const ServiceRegistry &services) {
  auto *appDb = services.appDb();
  auto *rootItemManager = services.rootItemManager();
  auto values = rootItemManager->getPreferenceValues(command.uniqueId());

  const auto resolve = [&](const glz::generic &v) -> glz::generic {
    if (!v.is_string()) return {};
    auto app = appDb->findById(QString::fromStdString(v.get_string()));
    if (!app) return {};
    return glz::generic::object_t{
        {"id", app->id().toStdString()},
        {"name", app->displayName().toStdString()},
        {"icon", app->iconUrl().toString().toStdString()},
        {"path", app->path().string()},
    };
  };

  for (const auto &pref : rootItemManager->getMergedItemPreferences(command.uniqueId())) {
    auto data = pref.data();
    auto *picker = std::get_if<Preference::AppPickerData>(&data);
    if (!picker) continue;
    auto it = values.find(pref.name().toStdString());
    if (it == values.end()) continue;

    if (picker->multiple) {
      glz::generic::array_t apps;
      if (it->second.is_array()) {
        for (const auto &id : it->second.get_array()) {
          if (auto app = resolve(id); !app.is_null()) apps.emplace_back(std::move(app));
        }
      }
      it->second = std::move(apps);
    } else {
      it->second = resolve(it->second);
    }
  }

  return values;
}

} // namespace

manager::LoadOptions extensionLoadOptions(const ExtensionCommand &command, const ServiceRegistry &services) {
  manager::LoadOptions opts;

  opts.env = manager::CommandEnv::Production;
  opts.entrypoint = command.manifest().entrypoint.string();
  opts.mode = command.mode() == CommandMode::CommandModeView ? manager::CommandMode::View
                                                             : manager::CommandMode::NoView;
  opts.extension_id = command.extensionId().toStdString();
  opts.vicinae_path = Omnicast::dataDir().string();
  opts.command_name = command.commandId().toStdString();
  opts.extension_name = command.repositoryName().toStdString();
  opts.owner_or_author_name = command.author().toStdString();
  opts.is_raycast = command.isRaycast();
  opts.preferences = resolvePreferenceValues(command, services);
  opts.launch_type = manager::LaunchType::User;

  opts.capabilities.browserExtension = !services.browserExtension()->browsers().empty();
  opts.capabilities.windowManagement = services.windowManager()->isCapable();
  opts.capabilities.wallpaper = services.wallpaperManager()->canSetWallpaper();
  opts.capabilities.fileSearch = services.fileService()->isAvailable();

  return opts;
}
