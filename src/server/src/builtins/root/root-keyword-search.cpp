#include "builtins/root/root-keyword-search.hpp"
#include <QDebug>
#include <QFutureWatcher>
#include <filesystem>
#include "actions/clipboard-actions.hpp"
#include "builtins/root/root-search-sources.hpp"
#include "extension/extension-command.hpp"
#include "extension/extension-load-options.hpp"
#include "extension/manager/extension-manager.hpp"
#include "root-search/extensions/extension-root-provider.hpp"
#include "service-registry.hpp"
#include "navigation-controller.hpp"
#include "common/context.hpp"
#include "services/root-item-manager/root-item-manager.hpp"
#include "ui/action-panel/action-panel-state.hpp"

namespace {

manager::LoadOptions keywordLoadOptions(const ExtensionCommand &command, const ServiceRegistry &services) {
  auto opts = extensionLoadOptions(command, services);
  opts.entrypoint = command.manifest().keywordEntrypoint.string();
  opts.mode = manager::CommandMode::NoView;
  return opts;
}

// Run an action of type Run from a keyword row in the extension, and show the
// text or the error that it returns in a HUD.
class RunKeywordAction : public AbstractAction {
public:
  RunKeywordAction(std::shared_ptr<ExtensionCommand> command, std::string id, const QString &title)
      : AbstractAction(title, ImageURL::builtin(BuiltinIcon::Play)), m_command(std::move(command)),
        m_id(std::move(id)) {}

  void execute(ApplicationContext *ctx) override {
    using Watcher = QFutureWatcher<std::expected<manager::KeywordActionResult, std::string>>;
    auto *registry = ServiceRegistry::instance();
    auto *watcher = new Watcher(registry);
    auto *navigation = ctx->navigation.get();

    QObject::connect(watcher, &Watcher::finished, registry, [watcher, navigation]() {
      watcher->deleteLater();
      auto result = watcher->result();

      if (!result) {
        navigation->showHud(QString::fromStdString(result.error()),
                            ImageURL::builtin(BuiltinIcon::XMarkCircle));
      } else if (result->error) {
        navigation->showHud(QString::fromStdString(*result->error),
                            ImageURL::builtin(BuiltinIcon::XMarkCircle));
      } else if (result->hud) {
        navigation->showHud(QString::fromStdString(*result->hud));
      } else {
        navigation->closeWindow();
      }
    });

    auto opts = keywordLoadOptions(*m_command, *registry);
    watcher->setFuture(registry->extensionManager()->client().manager()->runKeywordAction(opts, m_id));
  }

private:
  std::shared_ptr<ExtensionCommand> m_command;
  std::string m_id;
};

} // namespace

RootKeywordSearch::RootKeywordSearch(const ServiceRegistry &services, QObject *parent)
    : QObject(parent), m_services(services), m_rootItems(services.rootItemManager()),
      m_extensions(services.extensionManager()) {
  connect(m_rootItems, &RootItemManager::itemsChanged, this, &RootKeywordSearch::indexCommands);
  indexCommands();
}

void RootKeywordSearch::indexCommands() {
  m_commands.clear();

  for (const auto *provider : m_rootItems->extensions()) {
    for (const auto &cmd : provider->repository()->commands()) {
      auto command = std::dynamic_pointer_cast<ExtensionCommand>(cmd);
      if (!command || !command->manifest().keyword) continue;
      m_commands.emplace(command->manifest().keyword->toStdString(), std::move(command));
    }
  }
}

std::shared_ptr<ExtensionCommand> RootKeywordSearch::setQuery(std::string_view text) {
  ++m_generation;

  auto space = text.find(' ');
  if (space == std::string_view::npos) return nullptr;

  auto it = m_commands.find(std::string{text.substr(0, space)});
  if (it == m_commands.end()) return nullptr;

  auto command = it->second;
  if (!m_rootItems->itemMetadata(command->uniqueId()).enabled) return nullptr;

  auto opts = keywordLoadOptions(*command, m_services);

  using Watcher = QFutureWatcher<std::expected<manager::KeywordRows, std::string>>;
  auto *watcher = new Watcher(this);

  connect(watcher, &Watcher::finished, this, [this, watcher, command, generation = m_generation]() {
    watcher->deleteLater();
    if (generation != m_generation) return;

    auto result = watcher->result();
    if (!result) {
      qWarning() << "Keyword query failed for" << command->name() << result.error();
      return;
    }

    emit resultsReady({.command = command, .items = std::move(result->items)});
  });

  watcher->setFuture(
      m_extensions->client().manager()->queryKeyword(opts, std::string{text.substr(space + 1)}));

  return command;
}

const manager::KeywordItem *RootKeywordSection::item(int i) const {
  if (i < 0 || std::cmp_greater_equal(i, m_results.items.size())) return nullptr;
  return &m_results.items[i];
}

QString RootKeywordSection::sectionName() const {
  return m_results.command ? m_results.command->name() : QString();
}

QString RootKeywordSection::itemId(int i) const {
  const auto *row = item(i);
  if (!row) return {};
  return QStringLiteral("keyword:%1:%2").arg(QString::fromStdString(m_results.command->uniqueId())).arg(i);
}

QString RootKeywordSection::itemTitle(int i) const {
  const auto *row = item(i);
  return row ? QString::fromStdString(row->title) : QString();
}

QString RootKeywordSection::itemSubtitle(int i) const {
  const auto *row = item(i);
  return row && row->subtitle ? QString::fromStdString(*row->subtitle) : QString();
}

std::optional<ImageURL> RootKeywordSection::itemIcon(int i) const {
  const auto *row = item(i);
  if (!row) return {};
  if (!row->icon) return m_results.command->iconUrl();

  std::error_code ec;
  if (auto asset = m_results.command->assetPath() / *row->icon; std::filesystem::is_regular_file(asset, ec)) {
    return ImageURL::local(asset);
  }

  return ImageURL(ImageLikeModel(ExtensionImageModel{.source = QString::fromStdString(*row->icon)}));
}

QVariant RootKeywordSection::customData(int i, int role) const {
  const auto *row = item(i);
  if (role == AccessoryText && row && row->accessory) return QString::fromStdString(*row->accessory);
  return {};
}

QHash<int, QByteArray> RootKeywordSection::customRoleNames() const { return root_search::customRoleNames(); }

QHash<int, QVariant> RootKeywordSection::customRoleDefaults() const {
  return root_search::customRoleDefaults();
}

std::unique_ptr<ActionPanelState> RootKeywordSection::actionPanel(int i) const {
  const auto *row = item(i);
  if (!row || row->actions.empty()) return nullptr;

  auto panel = std::make_unique<ListActionPanelState>();
  auto *section = panel->createSection();

  for (const auto &action : row->actions) {
    Clipboard::Text const content{QString::fromStdString(action.content)};

    switch (action.type) {
    case manager::KeywordActionType::Copy:
      if (action.title) {
        section->addAction(new CopyToClipboardAction(content, QString::fromStdString(*action.title)));
      } else {
        section->addAction(new CopyToClipboardAction(content));
      }
      break;
    case manager::KeywordActionType::Paste:
      section->addAction(new PasteToFocusedWindowAction(content));
      break;
    case manager::KeywordActionType::Run:
      section->addAction(new RunKeywordAction(m_results.command, action.content,
                                              QString::fromStdString(action.title.value_or(""))));
      break;
    }
  }

  return panel;
}
