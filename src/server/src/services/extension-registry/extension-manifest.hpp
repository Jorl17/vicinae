#pragma once
#include <chrono>
#include <expected>
#include <filesystem>
#include "command/argument.hpp"
#include "command/command-types.hpp"
#include "command/preference.hpp"

struct ManifestError {
  QString m_message;

  ManifestError(const QString &message) : m_message(message) {}
};

struct ExtensionManifest {
  enum class Provenance { Local, Vicinae, Raycast };

  // The fields "keyword" and "keywordOptions" of a command, with the meaning of the
  // same settings of an Alfred script filter.
  struct Keyword {
    enum class Argument {
      // Show only the placeholder row until there is text after the keyword.
      Required,
      // Also run the handler for the keyword alone, with an empty query.
      Optional,
      // Run the handler for the keyword alone. Nothing may follow the keyword.
      None,
    };
    enum class QueueMode {
      // A new query aborts the running one.
      Terminate,
      // Let the running query finish, and then run only the newest text.
      Wait,
    };

    QString word;
    Argument argument = Argument::Required;
    // Whether a space must separate the query from the keyword.
    bool withSpace = true;
    // The row for the keyword before the handler returns. The title defaults to the
    // title of the command.
    std::optional<QString> title;
    std::optional<QString> subtitle;
    // The subtitle of that row while the handler runs, which defaults to subtitle.
    std::optional<QString> runningSubtitle;
    // The time after the last key press before we run the handler.
    std::chrono::milliseconds queueDelay{0};
    QueueMode queueMode = QueueMode::Terminate;
  };

  struct Command {
    QString name;
    QString title;
    QString description;
    CommandMode mode;
    std::vector<Preference> preferences;
    std::vector<QString> keywords;
    std::vector<CommandArgument> arguments;
    std::optional<QString> icon;
    std::optional<std::chrono::seconds> interval;
    std::filesystem::path entrypoint;
    // The keyword that runs keywordEntrypoint while the user types in the root search,
    // for example "calc" in "calc 2*3".
    std::optional<Keyword> keyword;
    std::filesystem::path keywordEntrypoint;
    bool defaultDisabled;
    Provenance provenance;
  };

  std::filesystem::path path;
  QString id;
  QString name;
  QString title;
  QString description;
  QString icon;
  QString author;
  std::vector<QString> categories;
  std::vector<Preference> preferences;
  std::vector<Command> commands;
  bool needsRaycastApi = false;
  Provenance provenance;

  static std::expected<ExtensionManifest, ManifestError> fromPackageJson(const std::filesystem::path &path);

  bool isFromRaycastStore() const { return provenance == ExtensionManifest::Provenance::Raycast; }
  bool isFromVicinaeStore() const { return provenance == ExtensionManifest::Provenance::Vicinae; }
  bool isLocal() const { return provenance == ExtensionManifest::Provenance::Local; }

private:
  static CommandArgument parseArgumentFromObject(const QJsonObject &obj);
  static Preference parsePreferenceFromObject(const QJsonObject &obj);
  static Command parseCommandFromObject(const QJsonObject &obj);
  static Keyword parseKeyword(const QString &word, const QJsonObject &options);
};
