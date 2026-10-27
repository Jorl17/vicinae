#include "builtins/root/keyword-match.hpp"

namespace {

bool isBlank(std::string_view text) { return text.find_first_not_of(' ') == std::string_view::npos; }

} // namespace

std::optional<KeywordMatch> matchKeyword(std::string_view text, const ExtensionManifest::Keyword &keyword) {
  using Argument = ExtensionManifest::Keyword::Argument;

  auto word = keyword.word.toStdString();
  if (!text.starts_with(word)) return std::nullopt;

  auto rest = text.substr(word.size());
  std::optional<std::string> query;

  if (keyword.argument == Argument::None) {
    if (!isBlank(rest)) return std::nullopt;
    query = "";
  } else if (rest.starts_with(' ')) {
    query = std::string{rest.substr(1)};
  } else if (rest.empty() || !keyword.withSpace) {
    query = std::string{rest};
  } else {
    return std::nullopt;
  }

  if (keyword.argument == Argument::Required && isBlank(*query)) query.reset();

  return KeywordMatch{.query = std::move(query)};
}

std::optional<std::string> keywordCompletion(std::string_view text,
                                             const ExtensionManifest::Keyword &keyword) {
  auto completion = keyword.word.toStdString();
  if (keyword.withSpace) completion += ' ';
  if (text.starts_with(completion)) return std::nullopt;
  return completion;
}
