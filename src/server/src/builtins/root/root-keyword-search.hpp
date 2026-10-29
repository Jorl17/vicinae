#pragma once
#include "generated/manager.hpp"
#include "ui/views/section-source.hpp"
#include <QCoreApplication>
#include <QObject>
#include <QTimer>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ApplicationContext;
class ExtensionCommand;
class ExtensionManager;
class KeywordApiSession;
class RootItemManager;
class ServiceRegistry;

/* The row that we show for a keyword before its handler returns rows, for
 * example "calc" with the subtitle "expr [using calc]". */
struct KeywordPlaceholder {
  QString title;
  QString subtitle;
  // The search text that Enter sets, or nullopt when Enter does nothing.
  std::optional<QString> completion;
};

/* What the root search shows for the keyword of an extension command: the rows
 * that its keyword handler returned for the last query, or else the placeholder
 * row. The placeholder says that the rows are coming, that there are none or why
 * the handler failed, so that the keyword always shows a row, as in Alfred. */
struct KeywordState {
  std::shared_ptr<ExtensionCommand> command;
  std::vector<manager::KeywordItem> items;
  std::optional<KeywordPlaceholder> placeholder;
};

/* Run the keyword handlers of extension commands for the text in the root
 * search, with the keyword options of the command (see
 * ExtensionManifest::Keyword). setText returns what to show at once, and
 * stateChanged gives the rows when the handler returns them. */
class RootKeywordSearch : public QObject {
  Q_OBJECT

signals:
  void stateChanged(const KeywordState &state) const;

public:
  explicit RootKeywordSearch(ApplicationContext &ctx, QObject *parent = nullptr);

  const KeywordState &setText(std::string_view text);

private:
  struct Match {
    std::shared_ptr<ExtensionCommand> command;
    // The text after the keyword, or nullopt when the argument is required and missing.
    std::optional<std::string> query;
  };

  void indexCommands();
  KeywordApiSession &apiSession(const std::shared_ptr<ExtensionCommand> &command);
  std::optional<Match> match(std::string_view text) const;
  void sendPendingQuery();
  void handleReply(std::uint64_t id, std::uint64_t generation,
                   const std::shared_ptr<ExtensionCommand> &command,
                   std::expected<manager::KeywordRows, std::string> result);

  ApplicationContext &m_ctx;
  const ServiceRegistry &m_services;
  RootItemManager *m_rootItems;
  ExtensionManager *m_extensions;
  std::unordered_map<std::string, std::shared_ptr<ExtensionCommand>> m_commands;
  // The API session of each command whose handler ran, by the unique id of the command.
  std::unordered_map<std::string, KeywordApiSession *> m_sessions;

  KeywordState m_state;
  // The placeholder row for the current text, before the handler gives its rows.
  KeywordPlaceholder m_keywordRow;
  // Whether the current text asks for rows, which it does not while a required argument is missing.
  bool m_wantsRows = false;
  std::optional<std::string> m_pendingQuery;
  QTimer m_queueDelay;
  // The id of the query that the handler runs, or 0.
  std::uint64_t m_runningQuery = 0;
  std::uint64_t m_lastQueryId = 0;
  // Changes with every change of the text.
  std::uint64_t m_generation = 0;
};

class RootKeywordSection : public SectionSource {
  Q_DECLARE_TR_FUNCTIONS(RootKeywordSection)

public:
  QString sectionName() const override;
  int count() const override;
  QString itemId(int i) const override;
  QString itemTitle(int i) const override;
  QString itemSubtitle(int i) const override;
  std::optional<ImageURL> itemIcon(int i) const override;
  QVariant customData(int i, int role) const override;
  QHash<int, QByteArray> customRoleNames() const override;
  QHash<int, QVariant> customRoleDefaults() const override;
  std::unique_ptr<ActionPanelState> actionPanel(int i) const override;

  void setState(KeywordState state) { m_state = std::move(state); }

private:
  const manager::KeywordItem *item(int i) const;
  bool showsPlaceholder() const { return m_state.items.empty() && m_state.placeholder.has_value(); }

  KeywordState m_state;
};
