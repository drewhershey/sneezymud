//////////////////////////////////////////////////////////////////////////
//
//  skill_training.cc
//
//  Skill training purchase system - allows players to pay gold to increase
//  their skill learnedness at guildmaster NPCs.
//
//////////////////////////////////////////////////////////////////////////

#include "skill_training.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "being.h"
#include "comm.h"
#include "defs.h"
#include "discipline.h"
#include "enum.h"
#include "extern.h"
#include "log.h"
#include "monster.h"
#include "parse.h"
#include "spell2.h"
#include "spells.h"
#include "sstring.h"
#include "structs.h"
#include "thing.h"
#include "tweaks.h"

namespace {

  // Helper for std::visit with multiple lambdas
  template <class... Ts>
  struct Overloaded : Ts... {
      using Ts::operator()...;
  };
  template <class... Ts>
  Overloaded(Ts...) -> Overloaded<Ts...>;

  constexpr int BASE_COST_PER_HONE = 10000;
  constexpr double ADVANCED_DISC_MULTIPLIER = 1.5;
  constexpr double TASK_COST_PER_LEVEL = 0.05;
  // Some universal skills use CLASS_ALL, while some use all individual class
  // bits except CLASS_COMMONER. This mask captures both cases.
  constexpr auto UNIVERSAL_CLASS_MASK =
    static_cast<unsigned short>(CLASS_ALL & ~CLASS_COMMONER);

  // GM level thresholds determine advanced discipline training caps.
  constexpr int GM_LEVEL_FULL_CAP = 100;
  constexpr int GM_LEVEL_MID_CAP = 80;
  constexpr int ADVANCED_GM_CAP_MID = 80;
  constexpr int ADVANCED_GM_CAP_LOW = 40;

  constexpr size_t MAX_COL_WIDTH = 22;
  constexpr size_t MIN_COL_WIDTH = 8;

  // Validated wrapper types - proof that data has been validated at parse time
  // If you have one of these, the underlying data is guaranteed valid.

  struct ValidatedSkill {
      spellNumT id;
      std::reference_wrapper<const spellInfo> info;

      [[nodiscard]] std::string_view name() const { return info.get().name; }
      [[nodiscard]] discNumT disc() const { return info.get().disc; }
  };

  struct ValidatedDisc {
      discNumT id;
      std::reference_wrapper<const disc_names_data> info;

      [[nodiscard]] std::string_view name() const { return info.get().name; }
      [[nodiscard]] std::string_view properName() const {
        return info.get().properName;
      }
      [[nodiscard]] unsigned short classNum() const {
        return info.get().class_num;
      }
  };

  // Scope variants - each carries validated data from parse time
  struct ScopeAll {};
  struct ScopeClassOnly {};
  struct ScopeSkill {
      ValidatedSkill skill;
      int requestedHones = 0;
  };
  struct ScopeDiscipline {
      ValidatedDisc discipline;
  };

  using TrainScope =
    std::variant<ScopeAll, ScopeClassOnly, ScopeSkill, ScopeDiscipline>;

  enum class TrainAction : uint8_t {
    HELP,
    LIST,
    PREVIEW,
    EXECUTE
  };

  struct TrainCommand {
      TrainAction action = TrainAction::HELP;
      TrainScope scope = ScopeAll{};
      bool targetNotFound = false;
  };

  struct TrainableSkill {
      ValidatedSkill skill;
      int current;
      int target;
      int hones;
      int cost;
  };

  enum class GuildmasterTier : uint8_t {
    FIRST,
    SECOND,
    THIRD
  };

  GuildmasterTier gmTier(const TMonster& gm) {
    const int level = gm.GetMaxLevel();
    if (level >= GM_LEVEL_FULL_CAP) {
      return GuildmasterTier::THIRD;
    }
    if (level >= GM_LEVEL_MID_CAP) {
      return GuildmasterTier::SECOND;
    }
    return GuildmasterTier::FIRST;
  }

  bool isBasicSkill(const ValidatedSkill& skill) {
    const auto& info = skill.info.get();
    return info.disc != info.assDisc;
  }

  bool isFastDisciplineSkill(const TBeing& ch, const ValidatedSkill& skill) {
    auto* disc = ch.getDiscipline(skill.disc());
    return disc && disc->isFast();
  }

  bool isAlwaysFullTrainable(const TBeing& ch, const ValidatedSkill& skill) {
    return isBasicSkill(skill) || isFastDisciplineSkill(ch, skill);
  }

  int advancedTrainingCap(const TMonster& gm) {
    switch (gmTier(gm)) {
      case GuildmasterTier::FIRST:
        return ADVANCED_GM_CAP_LOW;
      case GuildmasterTier::SECOND:
        return ADVANCED_GM_CAP_MID;
      case GuildmasterTier::THIRD:
        return MAX_SKILL_LEARNEDNESS;
    }
    return 0;
  }

  int skillTrainingCap(const TBeing& ch, const TMonster& gm,
    const ValidatedSkill& skill) {
    if (isAlwaysFullTrainable(ch, skill)) {
      return MAX_SKILL_LEARNEDNESS;
    }
    return advancedTrainingCap(gm);
  }

  int effectiveTrainingCap(const TBeing& ch, const TMonster& gm,
    const ValidatedSkill& skill) {
    return std::min({skillTrainingCap(ch, gm, skill),
      static_cast<int>(ch.getMaxSkillValue(skill.id)),
      static_cast<int>(MAX_SKILL_LEARNEDNESS)});
  }

  bool gmCanTrain(const TMonster& gm, const ValidatedDisc& disc) {
    return (disc.classNum() & gm.getClass()) != 0;
  }

  bool gmCanTrain(const TMonster& gm, discNumT disc) {
    return (discNames[disc].class_num & gm.getClass()) != 0;
  }

  // Bundles commonly-passed parameters
  struct TrainContext {
      std::reference_wrapper<TBeing> ch;
      std::reference_wrapper<TMonster> gm;
      std::reference_wrapper<const TrainCommand> cmd;

      [[nodiscard]] bool canTrain(const ValidatedDisc& disc) const {
        return gmCanTrain(gm, disc);
      }

      void tell(const sstring& msg) const {
        gm.get().doTell(fname(ch.get().getName()), msg);
      }

      void send(std::string_view msg) const { ch.get().sendTo(msg); }

      void sendPaged(const std::string& text) const {
        if (ch.get().desc) {
          ch.get().desc->page_string(text);
        } else {
          ch.get().sendTo(text);
        }
      }
  };

  sstring formatTalens(int64_t amount) {
    return sstring(std::to_string(amount)).comify();
  }

  int costPerHone(const ValidatedSkill& skill) {
    const auto& info = skill.info.get();
    double multiplier = tweakInfo[TWEAK_SKILLTRAININGCOST]->current;

    if (info.disc == info.assDisc) {
      multiplier *= ADVANCED_DISC_MULTIPLIER;
    }
    if (info.task > 0) {
      multiplier *= 1.0 + (static_cast<int>(info.task) * TASK_COST_PER_LEVEL);
    }

    return static_cast<int>(BASE_COST_PER_HONE * multiplier);
  }

  std::pair<int64_t, int64_t> sumSkills(
    const std::vector<TrainableSkill>& skills) {
    auto honesView = skills | std::views::transform(&TrainableSkill::hones);
    auto costView = skills | std::views::transform(&TrainableSkill::cost);
    return {std::accumulate(honesView.begin(), honesView.end(), 0L),
      std::accumulate(costView.begin(), costView.end(), 0L)};
  }

  enum class BlockReason : uint8_t {
    NONE,
    PLAYER_LACKS_SKILL,
    WRONG_GUILDMASTER,
    QUEST_INCOMPLETE,
    AT_MAX_POTENTIAL,
    AT_GM_CAP
  };

  BlockReason checkTrainability(const TBeing& ch, const ValidatedSkill& skill,
    const TMonster& gm) {
    const auto& info = skill.info.get();

    if (!ch.doesKnowSkill(skill.id) || ch.getRawNatSkillValue(skill.id) <= 0) {
      return BlockReason::PLAYER_LACKS_SKILL;
    }

    if (info.toggle && !ch.hasQuestBit(static_cast<int>(info.toggle))) {
      return BlockReason::QUEST_INCOMPLETE;
    }

    if (!gmCanTrain(gm, info.disc)) {
      return BlockReason::WRONG_GUILDMASTER;
    }

    const int current = ch.getRawNatSkillValue(skill.id);
    const int maxPotential = ch.getMaxSkillValue(skill.id);
    if (current >= maxPotential) {
      return BlockReason::AT_MAX_POTENTIAL;
    }

    if (current >= skillTrainingCap(ch, gm, skill)) {
      return BlockReason::AT_GM_CAP;
    }

    return BlockReason::NONE;
  }

  void explainBlock(const TrainContext& ctx, const ValidatedSkill& skill,
    BlockReason reason) {
    switch (reason) {
      case BlockReason::PLAYER_LACKS_SKILL:
        ctx.ch.get().sendTo(
          std::format("You don't know the {} skill.", skill.name()));
        break;
      case BlockReason::WRONG_GUILDMASTER:
        ctx.tell("I don't know enough about that ability to train you.");
        break;
      case BlockReason::QUEST_INCOMPLETE:
        ctx.tell(std::format("You haven't learned the secrets of {} yet.",
          skill.name()));
        break;
      case BlockReason::AT_MAX_POTENTIAL:
        ctx.tell(
          "You've already reached your maximum potential in that ability.");
        break;
      case BlockReason::AT_GM_CAP:
        ctx.tell(
          "I am not skilled enough to teach you more about that ability. You "
          "must seek a more experienced guildmaster.");
        break;
      case BlockReason::NONE:
        break;
    }
  }

  std::optional<TrainableSkill> buildTrainableSkill(const TrainContext& ctx,
    const ValidatedSkill& skill, int requestedHones = 0) {
    if (checkTrainability(ctx.ch, skill, ctx.gm) != BlockReason::NONE) {
      return std::nullopt;
    }

    const int current = ctx.ch.get().getRawNatSkillValue(skill.id);
    const int capTarget =
      effectiveTrainingCap(ctx.ch.get(), ctx.gm.get(), skill);

    // requestedHones = 0 means "train to max", otherwise train by that many
    const int target = (requestedHones > 0)
                         ? std::min(current + requestedHones, capTarget)
                         : capTarget;
    const int hones = target - current;

    return TrainableSkill{
      .skill = skill,
      .current = current,
      .target = target,
      .hones = hones,
      .cost = hones * costPerHone(skill),
    };
  }

  bool matchesScope(const ValidatedSkill& skill, const TrainScope& scope) {
    return std::visit(
      Overloaded{
        [](const ScopeAll&) { return true; },
        [&](const ScopeClassOnly&) {
          return (discNames[skill.disc()].class_num & UNIVERSAL_CLASS_MASK) !=
                 UNIVERSAL_CLASS_MASK;
        },
        [&](const ScopeSkill& s) { return skill.id == s.skill.id; },
        [&](
          const ScopeDiscipline& s) { return skill.disc() == s.discipline.id; },
      },
      scope);
  }

  // Validates a skill ID for bulk operations
  std::optional<ValidatedSkill> validateSkill(spellNumT id) {
    const auto* info = discArray[id];
    if (!info || !info->name) {
      return std::nullopt;
    }
    return ValidatedSkill{.id = id, .info = *info};
  }

  std::vector<TrainableSkill> collectSkills(const TrainContext& ctx) {
    if (const auto* s = std::get_if<ScopeSkill>(&ctx.cmd.get().scope)) {
      auto ts = buildTrainableSkill(ctx, s->skill, s->requestedHones);
      return ts ? std::vector{*ts} : std::vector<TrainableSkill>{};
    }

    std::vector<TrainableSkill> result;
    for (int i = static_cast<int>(MIN_SPELL); i < static_cast<int>(MAX_SKILL);
      ++i) {
      auto validated = validateSkill(static_cast<spellNumT>(i));
      if (!validated) {
        continue;
      }
      if (!matchesScope(*validated, ctx.cmd.get().scope)) {
        continue;
      }
      auto ts = buildTrainableSkill(ctx, *validated);
      if (ts) {
        result.emplace_back(*ts);
      }
    }
    return result;
  }

  std::optional<ValidatedSkill> findSkill(std::string_view name) {
    if (name.empty()) {
      return std::nullopt;
    }

    auto skills =
      std::views::iota(static_cast<int>(MIN_SPELL),
        static_cast<int>(MAX_SKILL)) |
      std::views::transform([](int i) { return static_cast<spellNumT>(i); });

    auto it = std::ranges::find_if(skills, [&](spellNumT s) {
      const auto* info = discArray[s];
      return info && info->name && is_abbrev(name, info->name);
    });

    if (it == std::ranges::end(skills)) {
      return std::nullopt;
    }
    return ValidatedSkill{.id = *it, .info = *discArray[*it]};
  }

  std::optional<ValidatedDisc> findDiscipline(std::string_view name) {
    if (name.empty()) {
      return std::nullopt;
    }

    auto discs =
      std::views::iota(static_cast<int>(MIN_DISC),
        static_cast<int>(MAX_DISCS)) |
      std::views::transform([](int i) { return static_cast<discNumT>(i); });

    auto it = std::ranges::find_if(discs, [&](discNumT d) {
      const auto& dn = discNames[d];
      return is_abbrev(name, dn.name) || is_abbrev(name, dn.properName);
    });

    if (it == std::ranges::end(discs)) {
      return std::nullopt;
    }
    return ValidatedDisc{.id = *it, .info = discNames[*it]};
  }

  TrainCommand parseListCommand(const sstring& argument) {
    TrainCommand cmd{.action = TrainAction::LIST};
    const sstring word2 = argument.word(2);

    if (word2.empty() || is_abbrev(word2, "all")) {
      return cmd;
    }
    if (is_abbrev(word2, "class")) {
      cmd.scope = ScopeClassOnly{};
      return cmd;
    }
    if (is_abbrev(word2, "skill")) {
      if (auto s = findSkill(argument.word(3))) {
        cmd.scope = ScopeSkill{.skill = *s};
      } else {
        cmd.targetNotFound = true;
      }
      return cmd;
    }
    if (is_abbrev(word2, "discipline")) {
      if (auto d = findDiscipline(argument.word(3))) {
        cmd.scope = ScopeDiscipline{.discipline = *d};
      } else {
        cmd.targetNotFound = true;
      }
      return cmd;
    }
    if (auto d = findDiscipline(word2)) {
      cmd.scope = ScopeDiscipline{.discipline = *d};
      return cmd;
    }
    if (auto s = findSkill(word2)) {
      cmd.scope = ScopeSkill{.skill = *s};
    }
    return cmd;
  }

  TrainCommand parseBulkCommand(const sstring& argument, TrainScope scope) {
    return {
      .action = is_abbrev(argument.word(2), "confirm") ? TrainAction::EXECUTE
                                                       : TrainAction::PREVIEW,
      .scope = scope,
    };
  }

  TrainCommand parseTargetCommand(const sstring& argument) {
    TrainCommand cmd;
    const sstring word1 = argument.word(1);

    const bool forceSkill = is_abbrev(word1, "skill");
    const bool forceDisc = is_abbrev(word1, "discipline");
    const int nameIdx = (forceSkill || forceDisc) ? 2 : 1;

    const sstring targetName = argument.word(nameIdx);
    if (targetName.empty()) {
      return cmd;
    }

    const sstring nextWord = argument.word(nameIdx + 1);
    const sstring afterWord = argument.word(nameIdx + 2);

    int requestedHones = 0;
    bool confirmed = is_abbrev(nextWord, "confirm");
    if (!confirmed && !nextWord.empty()) {
      requestedHones = convertTo<int>(nextWord);
      confirmed = is_abbrev(afterWord, "confirm");
    }

    cmd.action = confirmed ? TrainAction::EXECUTE : TrainAction::PREVIEW;

    if (!forceDisc) {
      if (auto s = findSkill(targetName)) {
        cmd.scope = ScopeSkill{.skill = *s, .requestedHones = requestedHones};
        return cmd;
      }
      if (forceSkill) {
        cmd.targetNotFound = true;
        return cmd;
      }
    }

    if (!forceSkill) {
      if (auto d = findDiscipline(targetName)) {
        cmd.scope = ScopeDiscipline{.discipline = *d};
        return cmd;
      }
    }

    cmd.targetNotFound = true;
    return cmd;
  }

  TrainCommand parseCommand(const sstring& argument) {
    const sstring word1 = argument.word(1);

    if (word1.empty()) {
      return {};
    }
    if (is_abbrev(word1, "list")) {
      return parseListCommand(argument);
    }
    if (is_abbrev(word1, "all")) {
      return parseBulkCommand(argument, ScopeAll{});
    }
    if (is_abbrev(word1, "class")) {
      return parseBulkCommand(argument, ScopeClassOnly{});
    }
    return parseTargetCommand(argument);
  }

  std::string scopeDescription(const TrainScope& scope, const TMonster& gm) {
    return std::visit(
      Overloaded{
        [](const ScopeAll&) { return std::string{"all"}; },
        [&](const ScopeClassOnly&) {
          return std::format("{} class", classInfo[gm.bestClass()].name);
        },
        [](const ScopeSkill& s) { return std::string{s.skill.name()}; },
        [](const ScopeDiscipline& s) {
          return std::string{s.discipline.properName()};
        },
      },
      scope);
  }

  std::string confirmCommand(const TrainScope& scope) {
    return std::visit(
      Overloaded{
        [](const ScopeAll&) { return std::string{"gain train all confirm"}; },
        [](const ScopeClassOnly&) {
          return std::string{"gain train class confirm"};
        },
        [](const ScopeSkill& s) {
          if (s.requestedHones > 0) {
            return std::format("gain train {} {} confirm", s.skill.name(),
              s.requestedHones);
          }
          return std::format("gain train {} confirm", s.skill.name());
        },
        [](const ScopeDiscipline& s) {
          return std::format("gain train {} confirm", s.discipline.name());
        },
      },
      scope);
  }

  void showHelp(TBeing& ch) {
    ch.sendTo(
      "Skill training commands:\n\r"
      "  gain train <skill>                 - Show cost to train a skill\n\r"
      "  gain train <skill> confirm         - Train skill and pay\n\r"
      "  gain train <skill> <hones>         - Show cost for partial "
      "training\n\r"
      "  gain train <skill> <hones> confirm - Train partially and pay\n\r"
      "  gain train <discipline>            - Show discipline training cost\n\r"
      "  gain train class                   - Show class skills cost\n\r"
      "  gain train all                     - Show total cost for all "
      "skills\n\r"
      "  gain train list [scope]            - Detailed breakdown\n\r");
  }

  std::string truncate(std::string_view str, size_t width) {
    if (str.length() <= width) {
      return std::string{str};
    }
    return std::string{str.substr(0, width - 3)} + "...";
  }

  std::string buildSkillTable(const std::vector<TrainableSkill>& skills,
    std::string_view title = {}) {
    std::string out;
    if (!title.empty()) {
      out = std::format("{}\n\r", title);
    }

    if (skills.empty()) {
      out += "You have no trainable skills in this category.\n\r";
      return out;
    }

    auto sorted = skills;
    std::ranges::sort(sorted, {}, [](const TrainableSkill& ts) {
      return std::pair{ts.skill.disc(), ts.skill.name()};
    });

    size_t skillColWidth = MIN_COL_WIDTH;
    size_t discColWidth = MIN_COL_WIDTH;
    for (const auto& ts : sorted) {
      skillColWidth = std::min(MAX_COL_WIDTH,
        std::max(skillColWidth, ts.skill.name().size()));
      discColWidth = std::min(MAX_COL_WIDTH,
        std::max(discColWidth, strlen(discNames[ts.skill.disc()].name)));
    }

    out += std::format("  {:<{}} {:<{}} {:>3} {:>3} {:>5} {:>10}\n\r", "Skill",
      skillColWidth, "Discipline", discColWidth, "Cur", "To", "Hones", "Cost");

    for (const auto& ts : sorted) {
      out += std::format("  {:<{}} {:<{}} {:>3} {:>3} {:>5} {:>10}\n\r",
        truncate(ts.skill.name(), skillColWidth), skillColWidth,
        truncate(discNames[ts.skill.disc()].name, discColWidth), discColWidth,
        ts.current, ts.target, ts.hones, formatTalens(ts.cost));
    }

    const auto [totalHones, totalCost] = sumSkills(sorted);
    out += std::format("\n\rTotal: {} skills, {} {}, {} talens.\n\r",
      sorted.size(), totalHones, singularOrPlural(totalHones, "hone"),
      formatTalens(totalCost));

    return out;
  }

  void showList(const TrainContext& ctx,
    const std::vector<TrainableSkill>& skills, std::string_view title) {
    ctx.sendPaged(buildSkillTable(skills, title));
  }

  void handleList(const TrainContext& ctx) {
    const std::string title = std::visit(
      Overloaded{
        [](
          const ScopeAll&) { return std::string{"Training list: all skills"}; },
        [&](const ScopeClassOnly&) {
          return std::format("Training list: {} skills",
            classInfo[ctx.gm.get().bestClass()].name);
        },
        [&](const ScopeSkill& s) {
          return std::format("Training list: {}", s.skill.name());
        },
        [&](const ScopeDiscipline& s) {
          if (!ctx.canTrain(s.discipline)) {
            ctx.send(std::format("You cannot train the {} discipline here.\n\r",
              s.discipline.properName()));
            return std::string{};
          }
          return std::format("Training list: {} discipline",
            s.discipline.properName());
        },
      },
      ctx.cmd.get().scope);

    if (title.empty()) {
      return;
    }

    showList(ctx, collectSkills(ctx), title);
  }

  bool validateTrainingScope(const TrainContext& ctx) {
    return std::visit(Overloaded{
                        [](const ScopeAll&) { return true; },
                        [](const ScopeClassOnly&) { return true; },
                        [&](const ScopeSkill& s) {
                          const auto block =
                            checkTrainability(ctx.ch, s.skill, ctx.gm);
                          if (block != BlockReason::NONE) {
                            explainBlock(ctx, s.skill, block);
                            return false;
                          }
                          return true;
                        },
                        [&](const ScopeDiscipline& s) {
                          if (!ctx.canTrain(s.discipline)) {
                            ctx.tell("I know nothing of that discipline.");
                            return false;
                          }
                          return true;
                        },
                      },
      ctx.cmd.get().scope);
  }

  std::string noTrainableSkillsMessage(const TrainContext& ctx) {
    return std::visit(
      Overloaded{
        [&](const ScopeSkill&) {
          return std::string{
            "You've already reached your maximum potential with that ability."};
        },
        [&](const auto&) {
          return std::format("You have no {} skills that need training.",
            scopeDescription(ctx.cmd.get().scope, ctx.gm));
        },
      },
      ctx.cmd.get().scope);
  }

  std::string previewIntro(const TrainContext& ctx) {
    return std::visit(
      Overloaded{
        [&](const ScopeAll&) {
          return std::string{
            "I can help you improve your skills. Limits depend on the "
            "ability."};
        },
        [&](const ScopeClassOnly&) {
          return std::format(
            "I can help you improve your {} skills. Limits depend on the "
            "ability.",
            classInfo[ctx.gm.get().bestClass()].name);
        },
        [&](const ScopeSkill& s) {
          const int cap =
            effectiveTrainingCap(ctx.ch.get(), ctx.gm.get(), s.skill);
          return std::format(
            "I can help you improve {}, up to a maximum of {}%.",
            s.skill.name(), cap);
        },
        [&](const ScopeDiscipline& s) {
          return std::format(
            "I can help you improve your skills in the {} discipline. Limits "
            "depend on the ability.",
            s.discipline.properName());
        },
      },
      ctx.cmd.get().scope);
  }

  void showPreview(const TrainContext& ctx,
    const std::vector<TrainableSkill>& skills) {
    if (const auto* s = std::get_if<ScopeSkill>(&ctx.cmd.get().scope)) {
      if (skills.size() == 1 && s->requestedHones > 0) {
        const auto& ts = skills[0];
        if (ts.hones < s->requestedHones) {
          ctx.tell(std::format("I can only train your {} by {} {} (to {}%).",
            ts.skill.name(), ts.hones, singularOrPlural(ts.hones, "hone"),
            ts.target));
        }
      }
    }

    ctx.tell(previewIntro(ctx));

    std::string out = buildSkillTable(skills);
    out += std::format("\n\rType '{}' to confirm.\n\r",
      confirmCommand(ctx.cmd.get().scope));
    ctx.sendPaged(out);
  }

  void applyTraining(TBeing& ch, const std::vector<TrainableSkill>& skills) {
    for (const auto& ts : skills) {
      const int newValue =
        std::min<int>(ts.target, ch.getMaxSkillValue(ts.skill.id));
      ch.setSkillValue(ts.skill.id, newValue);
      ch.setNatSkillValue(ts.skill.id, newValue);
    }
    ch.affectTotal();
  }

  void logTraining(const TrainContext& ctx,
    const std::vector<TrainableSkill>& skills, int cost) {
    if (skills.size() == 1) {
      const auto& ts = skills[0];
      vlogf(LOG_MISC,
        std::format("{} trained {} from {}% to {}% for {} talens at {} "
                    "(vnum {})",
          ctx.ch.get().getName(), ts.skill.name(), ts.current, ts.target, cost,
          ctx.gm.get().getName(), ctx.gm.get().mobVnum()));
    } else {
      vlogf(LOG_MISC,
        std::format("{} bulk trained {} {} skills for {} talens at {} "
                    "(vnum {})",
          ctx.ch.get().getName(), skills.size(),
          scopeDescription(ctx.cmd.get().scope, ctx.gm), cost,
          ctx.gm.get().getName(), ctx.gm.get().mobVnum()));
    }
  }

  void showResult(const TrainContext& ctx,
    const std::vector<TrainableSkill>& skills) {
    if (skills.size() == 1) {
      const auto& ts = skills[0];
      ctx.send(
        std::format("You have improved your {} by {} {} (from {}% to {}%).\n\r",
          ts.skill.name(), ts.hones, singularOrPlural(ts.hones, "hone"),
          ts.current, ts.target));
    } else {
      ctx.send(std::format("You have trained {} skills.\n\r", skills.size()));
    }
  }

  bool executeTraining(const TrainContext& ctx,
    const std::vector<TrainableSkill>& skills) {
    const auto [totalHones, totalCost] = sumSkills(skills);

    if (totalCost > std::numeric_limits<int>::max()) {
      ctx.tell("I simply can't teach you that much at once, sorry.");
      vlogf(LOG_BUG,
        std::format("{} attempted to bulk train {} skills exceeding max "
                    "cost at {} (vnum {})",
          ctx.ch.get().getName(), skills.size(), ctx.gm.get().getName(),
          ctx.gm.get().mobVnum()));
      return false;
    }

    const auto cost = static_cast<int>(totalCost);
    if (ctx.ch.get().getMoney() < cost) {
      ctx.send(std::format("You need {} talens to purchase that training.\n\r",
        formatTalens(cost)));
      return false;
    }

    act("$n places both $s hands on your head for a moment.", false,
      &ctx.gm.get(), nullptr, &ctx.ch.get(), TO_VICT);
    act("$n places both $s hands on $N's head for a moment.", false,
      &ctx.gm.get(), nullptr, &ctx.ch.get(), TO_NOTVICT);

    // Just remove money from player - don't give to GM, as they're killable.
    ctx.ch.get().addToMoney(-cost, GOLD_SHOP);
    applyTraining(ctx.ch.get(), skills);
    logTraining(ctx, skills, cost);
    showResult(ctx, skills);

    return true;
  }

  void handleTraining(const TrainContext& ctx) {
    if (!validateTrainingScope(ctx)) {
      return;
    }

    const auto skills = collectSkills(ctx);
    if (skills.empty()) {
      ctx.tell(noTrainableSkillsMessage(ctx));
      return;
    }

    if (ctx.cmd.get().action == TrainAction::PREVIEW) {
      showPreview(ctx, skills);
      return;
    }

    executeTraining(ctx, skills);
  }

}  // namespace

bool handleSkillTraining(TBeing& ch, TMonster& gm, const sstring& argument) {
  const auto cmd = parseCommand(argument);
  const TrainContext ctx{.ch = ch, .gm = gm, .cmd = cmd};

  switch (cmd.action) {
    case TrainAction::HELP:
      showHelp(ch);
      break;
    case TrainAction::LIST:
      if (cmd.targetNotFound) {
        ctx.tell("I don't know of any skill or discipline by that name.");
        break;
      }
      handleList(ctx);
      break;
    case TrainAction::PREVIEW:
    case TrainAction::EXECUTE:
      if (cmd.targetNotFound) {
        ctx.tell("I don't know of any skill or discipline by that name.");
        break;
      }
      handleTraining(ctx);
      break;
  }
  return true;
}
