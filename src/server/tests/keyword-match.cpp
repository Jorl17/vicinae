#include <catch2/catch_test_macros.hpp>
#include "builtins/root/keyword-match.hpp"

using Keyword = ExtensionManifest::Keyword;

// NOLINTBEGIN(bugprone-throwing-static-initialization)
TEST_CASE("a required argument shows the placeholder until there is text after the keyword") {
  Keyword const calc{.word = "calc"};

  REQUIRE(matchKeyword("calc", calc)->query == std::nullopt);
  REQUIRE(matchKeyword("calc ", calc)->query == std::nullopt);
  REQUIRE(matchKeyword("calc 2*3", calc)->query == "2*3");
  REQUIRE_FALSE(matchKeyword("calc2", calc));
  REQUIRE_FALSE(matchKeyword("cal", calc));
  REQUIRE_FALSE(matchKeyword("calculator", calc));
}

TEST_CASE("an optional argument runs for the keyword alone") {
  Keyword const emoji{.word = "emoji", .argument = Keyword::Argument::Optional};

  REQUIRE(matchKeyword("emoji", emoji)->query == "");
  REQUIRE(matchKeyword("emoji ", emoji)->query == "");
  REQUIRE(matchKeyword("emoji smile", emoji)->query == "smile");
  REQUIRE_FALSE(matchKeyword("emojis", emoji));
}

TEST_CASE("no argument runs for the keyword alone and nothing may follow it") {
  Keyword const ip{.word = "ip", .argument = Keyword::Argument::None, .withSpace = false};

  REQUIRE(matchKeyword("ip", ip)->query == "");
  REQUIRE(matchKeyword("ip ", ip)->query == "");
  REQUIRE_FALSE(matchKeyword("ip x", ip));
  REQUIRE_FALSE(matchKeyword("ipx", ip));
}

TEST_CASE("without the space, the text right after the keyword is the query") {
  Keyword const pw{.word = "pw", .argument = Keyword::Argument::Optional, .withSpace = false};

  REQUIRE(matchKeyword("pw24", pw)->query == "24");
  REQUIRE(matchKeyword("pw 24", pw)->query == "24");
}
TEST_CASE("enter on the placeholder row completes the keyword and keeps a typed query") {
  Keyword const calc{.word = "calc"};
  Keyword const ip{.word = "ip", .argument = Keyword::Argument::None, .withSpace = false};

  REQUIRE(keywordCompletion("calc", calc) == "calc ");
  REQUIRE(keywordCompletion("calc ", calc) == std::nullopt);
  REQUIRE(keywordCompletion("calc 2*3", calc) == std::nullopt);
  REQUIRE(keywordCompletion("ip", ip) == std::nullopt);
}
// NOLINTEND(bugprone-throwing-static-initialization)
