#include "builtins/root/root-keyword-search.hpp"
#include <QDebug>
#include <QFutureWatcher>
#include <filesystem>
#include "actions/clipboard-actions.hpp"
#include "builtins/root/keyword-api-session.hpp"
#include "builtins/root/keyword-match.hpp"
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

// Put the keyword in the search text, as Enter on a script filter keyword does in
// Alfred. The window stays open, and the new text runs the query.
class CompleteKeywordAction : public AbstractAction {
public:
  explicit CompleteKeywordAction(QString text)
      : AbstractAction(QCoreApplication::translate("CompleteKeywordAction", "Complete Keyword"),
                       ImageURL::builtin(BuiltinIcon::Text)),
        m_text(std::move(text)) {}

  void execute(ApplicationContext *ctx) override { ctx->navigation->setSearchText(m_text); }

private:
  QString m_text;
};

} // namespace

RootKeywordSearch::RootKeywordSearch(ApplicationContext &ctx, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_services(*ctx.services), m_rootItems(m_services.rootItemManager()),
      m_extensions(m_services.extensionManager()) {
  m_queueDelay.setSingleShot(true);
  connect(&m_queueDelay, &QTimer::timeout, this, &RootKeywordSearch::sendPendingQuery);
  connect(m_rootItems, &RootItemManager::itemsChanged, this, &RootKeywordSearch::indexCommands);
  indexCommands();
}

void RootKeywordSearch::indexCommands() {
  m_commands.clear();

  for (const auto *provider : m_rootItems->extensions()) {
    for (const auto &cmd : provider->repository()->commands()) {
      auto command = std::dynamic_pointer_cast<ExtensionCommand>(cmd);
      if (!command || !command->manifest().keyword) continue;
      m_commands.emplace(command->manifest().keyword->word.toStdString(), std::move(command));
    }
  }
}

// Find the command whose keyword starts the text. When several keywords match,
// the longest one wins.
std::optional<RootKeywordSearch::Match> RootKeywordSearch::match(std::string_view text) const {
  std::optional<Match> best;
  std::size_t bestLength = 0;

  for (const auto &[word, command] : m_commands) {
    if (word.size() <= bestLength) continue;
    auto found = matchKeyword(text, *command->manifest().keyword);
    if (!found || !m_rootItems->itemMetadata(command->uniqueId()).enabled) continue;

    best = Match{.command = command, .query = std::move(found->query)};
    bestLength = word.size();
  }

  return best;
}

const KeywordState &RootKeywordSearch::setText(std::string_view text) {
  ++m_generation;

  auto found = match(text);
  m_engaged = found && text.size() > found->command->manifest().keyword->word.toUtf8().size();

  if (!found) {
    m_queueDelay.stop();
    m_pendingQuery.reset();
    m_wantsRows = false;
    m_state = {};
    return m_state;
  }

  if (found->command != m_state.command) {
    m_state = {.command = found->command};
    m_runningQuery = 0;
  }

  const auto &keyword = *found->command->manifest().keyword;
  auto title = keyword.title.value_or(found->command->name());
  auto subtitle = keyword.subtitle.value_or(QString());
  auto completion = keywordCompletion(text, keyword).transform([](const std::string &s) {
    return QString::fromStdString(s);
  });

  m_keywordRow = KeywordPlaceholder{.title = title, .subtitle = subtitle, .completion = completion};

  if (!found->query) {
    m_queueDelay.stop();
    m_pendingQuery.reset();
    m_wantsRows = false;
    m_state.items.clear();
    m_state.placeholder = m_keywordRow;
    return m_state;
  }

  // While the user types after the same keyword, we keep the rows of the last
  // query until the next rows arrive, as Alfred does.
  m_wantsRows = true;
  if (m_state.items.empty()) {
    m_state.placeholder = m_keywordRow;
    m_state.placeholder->subtitle = keyword.runningSubtitle.value_or(subtitle);
  }

  m_pendingQuery = std::move(found->query);

  if (keyword.queueDelay.count() > 0) {
    m_queueDelay.start(keyword.queueDelay);
  } else {
    sendPendingQuery();
  }

  return m_state;
}

void RootKeywordSearch::sendPendingQuery() {
  if (!m_pendingQuery || !m_state.command || m_queueDelay.isActive()) return;

  using QueueMode = ExtensionManifest::Keyword::QueueMode;
  auto command = m_state.command;

  // In wait mode, handleReply sends the newest text when the running query finishes.
  if (command->manifest().keyword->queueMode == QueueMode::Wait && m_runningQuery != 0) return;

  auto query = *std::exchange(m_pendingQuery, std::nullopt);
  auto id = ++m_lastQueryId;
  m_runningQuery = id;

  using Watcher = QFutureWatcher<std::expected<manager::KeywordRows, std::string>>;
  auto *watcher = new Watcher(this);

  connect(watcher, &Watcher::finished, this, [this, watcher, id, command, generation = m_generation]() {
    watcher->deleteLater();
    handleReply(id, generation, command, watcher->result());
  });

  auto opts = keywordLoadOptions(*command, m_services);
  auto const &session = apiSession(command);
  watcher->setFuture(m_extensions->client().manager()->queryKeyword(session.id(), opts, query));
}

// The session of a command lives as long as the keyword search, so that the
// handler can call the API again in later queries. A new build of the extension
// gives a new command, which gets a new session.
KeywordApiSession &RootKeywordSearch::apiSession(const std::shared_ptr<ExtensionCommand> &command) {
  auto &session = m_sessions[command->uniqueId()];

  if (session && session->command() != command) {
    session->deleteLater();
    session = nullptr;
  }
  if (!session) session = new KeywordApiSession(command, m_ctx, this);

  return *session;
}

void RootKeywordSearch::handleReply(std::uint64_t id, std::uint64_t generation,
                                    const std::shared_ptr<ExtensionCommand> &command,
                                    std::expected<manager::KeywordRows, std::string> result) {
  using QueueMode = ExtensionManifest::Keyword::QueueMode;

  if (id == m_runningQuery) m_runningQuery = 0;

  if (!result) qWarning() << "Keyword query failed for" << command->name() << result.error();

  // In terminate mode a newer query replaces this one, and in wait mode we show
  // these rows until the rows of the newest text arrive.
  bool const current = generation == m_generation;
  bool const shown = current || command->manifest().keyword->queueMode == QueueMode::Wait;

  if (command == m_state.command && m_wantsRows && shown) {
    if (result && !result->items.empty()) {
      m_state.items = std::move(result->items);
      m_state.placeholder.reset();
    } else {
      // Without rows, the keyword row says why, as Alfred does when a script
      // filter returns nothing.
      m_state.items.clear();
      m_state.placeholder = m_keywordRow;
      if (!result) {
        m_state.placeholder->subtitle = QString::fromStdString(result.error());
      } else if (result->error) {
        m_state.placeholder->subtitle = QString::fromStdString(*result->error);
      } else {
        m_state.placeholder->subtitle = tr("No results");
      }
    }
    emit stateChanged(m_state);
  }

  sendPendingQuery();
}

const manager::KeywordItem *RootKeywordSection::item(int i) const {
  if (i < 0 || std::cmp_greater_equal(i, m_state.items.size())) return nullptr;
  return &m_state.items[i];
}

int RootKeywordSection::count() const {
  return showsPlaceholder() ? 1 : static_cast<int>(m_state.items.size());
}

// Without a name, the list shows no header above the rows.
QString RootKeywordSection::sectionName() const {
  return m_state.command && m_sectionTitle ? m_state.command->name() : QString();
}

QString RootKeywordSection::itemId(int i) const {
  if (!m_state.command || i < 0 || i >= count()) return {};
  auto id = QString::fromStdString(m_state.command->uniqueId());
  if (showsPlaceholder()) return QStringLiteral("keyword:%1:placeholder").arg(id);
  return QStringLiteral("keyword:%1:%2").arg(id).arg(i);
}

QString RootKeywordSection::itemTitle(int i) const {
  if (showsPlaceholder()) return i == 0 ? m_state.placeholder->title : QString();
  const auto *row = item(i);
  return row ? QString::fromStdString(row->title) : QString();
}

QString RootKeywordSection::itemSubtitle(int i) const {
  if (showsPlaceholder()) return i == 0 ? m_state.placeholder->subtitle : QString();
  const auto *row = item(i);
  return row && row->subtitle ? QString::fromStdString(*row->subtitle) : QString();
}

std::optional<ImageURL> RootKeywordSection::itemIcon(int i) const {
  if (showsPlaceholder()) return m_state.command->iconUrl();
  const auto *row = item(i);
  if (!row) return {};
  if (!row->icon) return m_state.command->iconUrl();

  std::error_code ec;
  if (auto asset = m_state.command->assetPath() / *row->icon; std::filesystem::is_regular_file(asset, ec)) {
    return ImageURL::local(asset);
  }

  return ImageURL(ImageLikeModel(ExtensionImageModel{.source = QString::fromStdString(*row->icon)}));
}

QVariant RootKeywordSection::customData(int i, int role) const {
  if (role == SubtitleBelow) return m_subtitleBelowTitle;
  const auto *row = item(i);
  if (role == AccessoryText && row && row->accessory) return QString::fromStdString(*row->accessory);
  return {};
}

QHash<int, QByteArray> RootKeywordSection::customRoleNames() const { return root_search::customRoleNames(); }

QHash<int, QVariant> RootKeywordSection::customRoleDefaults() const {
  return root_search::customRoleDefaults();
}

std::unique_ptr<ActionPanelState> RootKeywordSection::actionPanel(int i) const {
  if (showsPlaceholder()) {
    if (!m_state.placeholder->completion) return nullptr;
    auto panel = std::make_unique<ListActionPanelState>();
    panel->createSection()->addAction(new CompleteKeywordAction(*m_state.placeholder->completion));
    return panel;
  }

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
      section->addAction(new RunKeywordAction(m_state.command, action.content,
                                              QString::fromStdString(action.title.value_or(""))));
      break;
    }
  }

  return panel;
}
