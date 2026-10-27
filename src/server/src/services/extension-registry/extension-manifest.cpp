#include "extension-manifest.hpp"
#include "internal/glaze-qt.hpp"
#include "utils.hpp"
#include "vicinae.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <charconv>

namespace fs = std::filesystem;

std::expected<ExtensionManifest, ManifestError> ExtensionManifest::fromPackageJson(const fs::path &path) {
  static const std::set<CommandMode> supportedModes{CommandMode::CommandModeView, CommandModeNoView};
  fs::path const manifestPath = path / "package.json";

  if (!fs::exists(manifestPath)) {
    return std::unexpected<ManifestError>(
        QString("Could not find package.json file at %1").arg(manifestPath.c_str()));
  }

  QFile file(manifestPath);

  if (!file.open(QIODevice::ReadOnly)) {
    return std::unexpected<ManifestError>(QString("Failed to open %1 for read").arg(manifestPath.c_str()));
  }

  QJsonParseError jsonError;
  auto json = QJsonDocument::fromJson(file.readAll(), &jsonError);

  if (jsonError.error) {
    return std::unexpected<ManifestError>(
        QString("Failed to parse package.json at %1").arg(manifestPath.c_str()));
  }

  ExtensionManifest manifest;
  auto obj = json.object();
  auto deps = obj.value("dependencies").toObject();

  manifest.path = path;
  manifest.id = QString::fromStdString(getLastPathComponent(path));
  manifest.name = obj.value("name").toString();
  manifest.title = obj.value("title").toString();
  manifest.description = obj.value("description").toString();
  manifest.icon = obj.value("icon").toString();
  manifest.author = obj.value("author").toString();
  manifest.needsRaycastApi = deps.contains(Omnicast::RAYCAST_NPM_API_PACKAGE);

  if (manifest.id.startsWith("store.vicinae.")) {
    manifest.provenance = ExtensionManifest::Provenance::Vicinae;
  } else if (manifest.id.startsWith("store.raycast.")) {
    manifest.provenance = ExtensionManifest::Provenance::Raycast;
  } else {
    manifest.provenance = ExtensionManifest::Provenance::Local;
  }

  for (const auto &obj : obj.value("categories").toArray()) {
    manifest.categories.emplace_back(obj.toString());
  }

  for (const auto &obj : obj.value("commands").toArray()) {
    auto command = parseCommandFromObject(obj.toObject());

    command.provenance = manifest.provenance;
    command.entrypoint = path / std::format("{}.js", command.name.toStdString());
    command.keywordEntrypoint = path / std::format("{}.keyword.js", command.name.toStdString());

    if (supportedModes.contains(command.mode)) { manifest.commands.emplace_back(command); }
  }

  for (const auto &obj : obj.value("preferences").toArray()) {
    manifest.preferences.emplace_back(parsePreferenceFromObject(obj.toObject()));
  }

  return manifest;
}

Preference ExtensionManifest::parsePreferenceFromObject(const QJsonObject &obj) {
  auto type = obj["type"].toString();
  Preference base;

  base.setTitle(obj["title"].toString());
  base.setDescription(obj["description"].toString());
  base.setName(obj["name"].toString());
  base.setPlaceholder(obj["placeholder"].toString());
  base.setRequired(obj["required"].toBool());
  if (obj.contains("default")) base.setDefaultValue(qJsonValueToGlazeGeneric(obj.value("default")));

  if (type == "textfield") {
    base.setData(Preference::TextData());
  } else if (type == "password") {
    base.setData(Preference::PasswordData());
  } else if (type == "checkbox") {
    auto checkbox = Preference::CheckboxData(obj["label"].toString());
    base.setData(checkbox);
  } else if (type == "appPicker") {
    base.setData(Preference::AppPickerData{.multiple = obj["multiple"].toBool()});
  } else if (type == "file") {
    base.setData(Preference::FilePickerData{.multiple = obj["multiple"].toBool()});
  } else if (type == "directory") {
    base.setData(Preference::DirectoryPickerData{.multiple = obj["multiple"].toBool()});
  } else if (type == "dropdown") {
    auto data = obj["data"].toArray();
    std::vector<Preference::DropdownData::Option> options;

    options.reserve(data.size());

    for (const auto &child : data) {
      auto obj = child.toObject();

      options.push_back({.title = obj["title"].toString(), .value = obj["value"].toString()});
    }

    base.setData(Preference::DropdownData{options});
    if (obj.contains("default")) base.setDefaultValue(qJsonValueToGlazeGeneric(obj.value("default")));
  } else {
    qWarning() << "Unknown extension preference type" << type;
  }

  return base;
}

CommandArgument ExtensionManifest::parseArgumentFromObject(const QJsonObject &obj) {
  CommandArgument arg;
  QString const type = obj.value("type").toString();

  if (type == "text") arg.type = CommandArgument::Text;
  if (type == "password") arg.type = CommandArgument::Password;
  if (type == "dropdown") arg.type = CommandArgument::Dropdown;

  arg.name = obj.value("name").toString();
  arg.placeholder = obj.value("placeholder").toString();
  arg.required = obj.value("required").toBool(false);

  if (type == "dropdown") {
    auto data = obj.value("data").toArray();
    std::vector<CommandArgument::DropdownData> options;

    options.reserve(data.size());

    for (const auto &child : data) {
      auto obj = child.toObject();

      options.push_back({.title = obj["title"].toString(), .value = obj["value"].toString()});
    }

    arg.data = options;
  }

  return arg;
}

static std::expected<std::chrono::seconds, QString> parseInterval(const std::string &str) {
  if (str.empty()) return std::unexpected(QString("interval is empty"));

  char unit = str.back();

  std::int64_t value = 0;
  auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size() - 1, value);
  if (ec != std::errc{} || ptr != str.data() + str.size() - 1)
    return std::unexpected(QString("invalid interval format: %1").arg(str.c_str()));

  using namespace std::chrono;

  seconds secs;

  switch (unit) {
  case 's':
    secs = seconds(value);
    break;
  case 'm':
    secs = duration_cast<seconds>(minutes(value));
    break;
  case 'h':
    secs = duration_cast<seconds>(hours(value));
    break;
  case 'd':
    secs = duration_cast<seconds>(hours(value * 24));
    break;
  default:
    return std::unexpected(QString("unknown interval unit: %1").arg(unit));
  }

  if (secs < seconds(5))
    return std::unexpected(QString("interval must be at least 5s, got %1s").arg(secs.count()));

  return secs;
}

ExtensionManifest::Keyword ExtensionManifest::parseKeyword(const QString &word, const QJsonObject &options) {
  Keyword keyword{.word = word};

  const auto optionalString = [&](QStringView name) -> std::optional<QString> {
    if (!options.value(name).isString()) return std::nullopt;
    return options.value(name).toString();
  };

  if (auto argument = options.value("argument").toString(); argument == "optional") {
    keyword.argument = Keyword::Argument::Optional;
  } else if (argument == "none") {
    keyword.argument = Keyword::Argument::None;
  } else if (!argument.isEmpty() && argument != "required") {
    qWarning() << "Unknown keyword argument" << argument << "for keyword" << word;
  }

  if (auto mode = options.value("queueMode").toString(); mode == "wait") {
    keyword.queueMode = Keyword::QueueMode::Wait;
  } else if (!mode.isEmpty() && mode != "terminate") {
    qWarning() << "Unknown keyword queue mode" << mode << "for keyword" << word;
  }

  // "immediate" or a number of milliseconds
  if (auto delay = options.value("queueDelay"); delay.isDouble()) {
    keyword.queueDelay = std::chrono::milliseconds(std::max(0, delay.toInt()));
  }

  keyword.withSpace = options.value("withSpace").toBool(true);
  keyword.title = optionalString(u"title");
  keyword.subtitle = optionalString(u"subtitle");
  keyword.runningSubtitle = optionalString(u"runningSubtitle");

  return keyword;
}

ExtensionManifest::Command ExtensionManifest::parseCommandFromObject(const QJsonObject &obj) {
  ExtensionManifest::Command command;
  auto type = obj.value("mode");

  command.name = obj.value("name").toString();
  command.title = obj.value("title").toString();
  command.description = obj.value("description").toString();
  command.defaultDisabled = obj.value("disabledByDefault").toBool(false);

  if (obj.contains("icon")) { command.icon = obj.value("icon").toString(); }
  if (auto keyword = obj.value("keyword").toString(); !keyword.isEmpty()) {
    command.keyword = parseKeyword(keyword, obj.value("keywordOptions").toObject());
  }

  if (type == "view") {
    command.mode = CommandMode::CommandModeView;
  } else if (type == "no-view") {
    command.mode = CommandMode::CommandModeNoView;
  } else {
    command.mode = CommandMode::CommandModeInvalid;
  }

  if (obj.contains("interval") && command.mode == CommandModeNoView) {
    auto interval = parseInterval(obj.value("interval").toString().toStdString());
    if (interval) {
      command.interval = *interval;
    } else {
      qWarning() << "Failed to parse interval for command" << command.name << "-" << interval.error();
    }
  }

  for (const auto &obj : obj.value("keywords").toArray()) {
    command.keywords.emplace_back(obj.toString());
  }

  for (const auto &obj : obj.value("preferences").toArray()) {
    command.preferences.emplace_back(parsePreferenceFromObject(obj.toObject()));
  }

  for (const auto &obj : obj.value("arguments").toArray()) {
    command.arguments.emplace_back(parseArgumentFromObject(obj.toObject()));
  }

  return command;
}
