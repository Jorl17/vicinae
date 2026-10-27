#pragma once
#include <optional>
#include <string>
#include <string_view>
#include "services/extension-registry/extension-manifest.hpp"

struct KeywordMatch {
  // The text after the keyword, or nullopt when the argument is required and missing.
  std::optional<std::string> query;
};

/* Return how `text` in the root search uses a keyword, by the rules of an Alfred
 * script filter, or nullopt when the text does not use it. */
std::optional<KeywordMatch> matchKeyword(std::string_view text, const ExtensionManifest::Keyword &keyword);

/* Return the search text that Enter on the placeholder row of a keyword sets, as
 * Alfred completes a keyword: the keyword and a space, or the keyword alone when
 * withSpace is false. Return nullopt when `text` already starts with it, so that
 * Enter does not remove a query that the user typed. */
std::optional<std::string> keywordCompletion(std::string_view text,
                                             const ExtensionManifest::Keyword &keyword);
