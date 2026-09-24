#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "ad/core/battle.hpp"
#include "ad/core/commands.hpp"

namespace ad::core {

/// The wire form of the commands (docs/PROTOCOL.md §5): compact JSON
/// objects with a `kind`. The campaign commands carry their player id -
/// the relay stamps the sender's side and the receiver checks the two
/// agree - and unit types travel as catalog ids, which both clients of a
/// match share (the world hash is checked at pairing).

[[nodiscard]] std::string commandToJson(const Command& cmd);
[[nodiscard]] std::expected<Command, std::string> commandFromJson(std::string_view text);

[[nodiscard]] std::string battleCommandToJson(const BattleCommand& cmd);
[[nodiscard]] std::expected<BattleCommand, std::string> battleCommandFromJson(std::string_view text);

[[nodiscard]] std::string battleOutcomeToJson(const BattleOutcome& outcome);
[[nodiscard]] std::expected<BattleOutcome, std::string> battleOutcomeFromJson(std::string_view text);

/// The `kind` of a command document without decoding it fully ("" when it
/// is not an object with a string kind).
[[nodiscard]] std::string commandKind(std::string_view text);

} // namespace ad::core
