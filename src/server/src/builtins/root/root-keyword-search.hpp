#pragma once
#include "generated/manager.hpp"
#include "ui/views/section-source.hpp"
#include <QCoreApplication>
#include <QObject>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ExtensionCommand;
class ExtensionManager;
class RootItemManager;
class ServiceRegistry;

/* The rows that the keyword handler of an extension command returned for the
 * text in the root search, for example the answer of "calc 2*3". */
struct KeywordResults {
  std::shared_ptr<ExtensionCommand> command;
  std::vector<manager::KeywordItem> items;
};

/* Run the keyword handlers of extension commands for the text in the root
 * search. When the text starts with the keyword of an enabled command and a
 * space, we send the rest of the text to the handler of that command and emit
 * resultsReady with its rows. We ignore the rows of a query when the text
 * changed while the handler ran. */
class RootKeywordSearch : public QObject {
  Q_OBJECT

signals:
  void resultsReady(const KeywordResults &results) const;

public:
  explicit RootKeywordSearch(const ServiceRegistry &services, QObject *parent = nullptr);

  /* Start a query for `text`. Return the command whose keyword starts the text,
   * or nullptr when there is none. */
  std::shared_ptr<ExtensionCommand> setQuery(std::string_view text);

private:
  void indexCommands();

  const ServiceRegistry &m_services;
  RootItemManager *m_rootItems;
  ExtensionManager *m_extensions;
  std::unordered_map<std::string, std::shared_ptr<ExtensionCommand>> m_commands;
  std::uint64_t m_generation = 0;
};

class RootKeywordSection : public SectionSource {
  Q_DECLARE_TR_FUNCTIONS(RootKeywordSection)

public:
  QString sectionName() const override;
  int count() const override { return static_cast<int>(m_results.items.size()); }
  QString itemId(int i) const override;
  QString itemTitle(int i) const override;
  QString itemSubtitle(int i) const override;
  std::optional<ImageURL> itemIcon(int i) const override;
  QVariant customData(int i, int role) const override;
  QHash<int, QByteArray> customRoleNames() const override;
  QHash<int, QVariant> customRoleDefaults() const override;
  std::unique_ptr<ActionPanelState> actionPanel(int i) const override;

  void setResults(KeywordResults results) { m_results = std::move(results); }
  const std::shared_ptr<ExtensionCommand> &command() const { return m_results.command; }

private:
  const manager::KeywordItem *item(int i) const;

  KeywordResults m_results;
};
