# Skill Learnedness and Success System

This document provides a comprehensive reference for the skill learning, learnedness, and success mechanics in SneezyMUD.

## Overview

The skill system consists of several interconnected components:

1. **Skills** - Individual abilities (spells, combat skills, etc.) stored in `CSkill` objects
2. **Disciplines** - Categories that group related skills, stored in `CDiscipline` objects
3. **Skill Configuration** - Per-skill settings in `discArray[]` via `spellInfo` structs
4. **Success Checks** - The `bSuccess()` function that determines if a skill attempt succeeds
5. **Learning Mechanics** - How skills and disciplines improve through practice and use

## Key Files

| File | Purpose |
|------|---------|
| `code/code/misc/skills.h` | `CSkill` class definition |
| `code/code/misc/discipline.h` | `CDiscipline` class, `discNumT` enum, constants |
| `code/code/misc/spell2.h` | `spellInfo` class, configuration enums (`discStartT`, `discLearnT`, etc.) |
| `code/code/misc/spell_info.cc` | `discArray[]` skill configuration database |
| `code/code/misc/discipline.cc` | `bSuccess()`, `learnFromDoing()`, `getMaxSkillValue()` |
| `code/code/misc/skills.cc` | `getSkillValue()`, `doesKnowSkill()`, skill accessors |
| `code/code/misc/gaining.cc` | Discipline training, `raiseDiscOnce()`, `CalcRaiseDisc()` |
| `code/code/misc/skill_dam.cc` | `getSkillDiffModifier()` |
| `code/code/disc/disc_*.h` | Discipline subclass definitions |

## Data Structures

### CSkill (Individual Skill Storage)

Location: `code/code/misc/skills.h`

```cpp
class CSkill {
  public:
    int value;        // Bit-packed storage for learnedness values
    time_t lastUsed;  // Timestamp for learn-by-doing cooldown

    int getLearnedness();         // Current effective learnedness (0-100)
    void setLearnedness(int n);
    int getNatLearnedness();      // Natural/trained learnedness (0-100)
    void setNatLearnedness(int n);
};
```

The `value` field uses bit manipulation (`GET_BITS_CORRECT`/`SET_BITS_CORRECT`) to store:
- Bits 0-7: Current learnedness (may include equipment/buff modifiers)
- Bits 8-15: Natural learnedness (trained value)

### CDiscipline (Discipline Storage)

Location: `code/code/misc/discipline.h`

```cpp
class CDiscipline {
  private:
    int uNatLearnedness;  // Natural discipline learnedness (0-100)
    int uLearnedness;     // Current learnedness (with modifiers)
    int uDoLearnedness;   // Learn-by-doing progress for this discipline

  public:
    int ok_for_class;     // Class restrictions

    virtual bool isBasic();      // True for basic disciplines (1 prac/point)
    virtual bool isFast();       // True for fast disciplines (up to 5 points/prac)
    virtual bool isAutomatic();  // True for auto-learning disciplines

    int getDoLearnedness() const;
    void setDoLearnedness(int);
    void setNatLearnedness(int);
    int getNatLearnedness() const;
    int getLearnedness() const;
    void setLearnedness(int);
};
```

Players have a `CMasterDiscipline` containing an array of all disciplines:
```cpp
class CMasterDiscipline {
  public:
    CDiscipline* disc[MAX_DISCS];
};
```

### spellInfo (Skill Configuration)

Location: `code/code/misc/spell2.h`

Each skill's configuration is stored in a `spellInfo` struct within `discArray[]`:

```cpp
class spellInfo {
  public:
    const char* name;           // Skill name
    statTypeT modifierStat;     // Primary stat for this skill
    int start;                  // discStartT - discipline level to begin learning
    int learn;                  // discLearnT - skill max increase rate
    skillUseClassT typ;         // Skill type (SPELL_MAGE, SKILL_WARRIOR, etc.)
    taskDiffT task;             // Difficulty (TASK_TRIVIAL to TASK_IMPOSSIBLE)
    lag_t lag;                  // Command lag
    discNumT disc;              // Primary discipline
    discNumT assDisc;           // Associated (advanced) discipline
    int16_t startLearnDo;       // discStartDoT - when learn-by-doing starts
    int16_t amtLearnDo;         // discLearnDoT - learn-by-doing amount
    int learnDoDiff;            // Learn-by-doing difficulty modifier
    // ... many other fields for targeting, components, costs, etc.
};
```

## Configuration Enums

### discStartT (START_*)

Location: `code/code/misc/spell2.h:262-330`

Defines the **discipline learnedness required to begin learning a skill**.

```cpp
enum discStartT {
  START_0 = 0,    // Available immediately
  START_1 = 1,
  START_5 = 5,
  START_10 = 10,
  // ... up to
  START_100 = 100 // Requires fully maxed discipline
};
```

**Example**: A skill with `START_30` requires the parent discipline to be at 30% or higher before the skill becomes available.

### discLearnT (LEARN_*)

Location: `code/code/misc/spell2.h:332-360`

Defines the **rate at which a skill's maximum increases** per discipline point above the start threshold.

```cpp
enum discLearnT {
  LEARN_0 = 0,
  LEARN_1 = 1,
  LEARN_2 = 2,
  // ... up to
  LEARN_100 = MAX_SKILL_LEARNEDNESS  // 100
};
```

**Example**: A skill with `START_30, LEARN_5` at discipline 50 would have:
- `tmp2 = 50 - 30 + 1 = 21`
- `maxSkill = min(5 * 21, 100) = 100`

### discStartDoT (START_DO_*)

Location: `code/code/misc/spell2.h:362-375`

Defines the **discipline learnedness required for learn-by-doing** to begin.

```cpp
enum discStartDoT {
  START_DO_NO = -1,   // No learn-by-doing
  START_DO_1 = 1,
  START_DO_10 = 10,
  START_DO_20 = 20,
  // ... up to
  START_DO_60 = 60
};
```

### discLearnDoT (LEARN_DO_*)

Location: `code/code/misc/spell2.h:377-386`

Defines the **amount gained per learn-by-doing event**.

```cpp
enum discLearnDoT {
  LEARN_DO_NO = -1,   // No learn-by-doing
  LEARN_DO_1 = 1,
  LEARN_DO_2 = 2,
  LEARN_DO_3 = 3,
  LEARN_DO_4 = 4,
  LEARN_DO_5 = 5,
  LEARN_DO_7 = 7,
  LEARN_DO_10 = 10
};
```

### taskDiffT (TASK_*)

Location: `code/code/misc/spell2.h:85-93`

Defines **skill difficulty** which affects base success rate.

```cpp
enum taskDiffT {
  TASK_TRIVIAL,    // 110% base (guaranteed at max learn)
  TASK_EASY,       // 100% base
  TASK_NORMAL,     // 90% base
  TASK_DIFFICULT,  // 80% base
  TASK_DANGEROUS,  // 70% base
  TASK_HOPELESS,   // 50% base
  TASK_IMPOSSIBLE  // 35% base
};
```

## Key Constants

Location: `code/code/misc/discipline.h:35-40`

```cpp
const byte MAX_SKILL_LEARNEDNESS = 100;   // Maximum skill value
const byte MAX_DISC_LEARNEDNESS = 100;    // Maximum discipline value
const byte LEARNEDNESS_STEP = 1;          // Minimum learning increment
const int PRACS_TO_MAX = 60;              // Practice sessions to max a standard discipline
```

## Core Functions

### getMaxSkillValue()

Location: `code/code/misc/discipline.cc:2544-2569`

Calculates the **maximum possible skill value** based on discipline learnedness.

**Formula:**
```
tmp2 = max(0, disciplineLearnedness - skill.start + 1)
maxSkill = min(skill.learn * tmp2, MAX_SKILL_LEARNEDNESS)
```

**Example:**
- Skill configuration: `START_25, LEARN_10`
- Discipline learnedness: 50
- `tmp2 = max(0, 50 - 25 + 1) = 26`
- `maxSkill = min(10 * 26, 100) = 100`

If discipline is below the start threshold, `maxSkill = 0` (skill not known).

### getSkillValue()

Location: `code/code/misc/skills.cc:1458-1489`

Returns the **current effective skill value**, accounting for:
1. Raw stored learnedness
2. Maximum skill cap (from discipline)
3. Skill apply modifiers (from equipment, buffs)

```cpp
short TBeing::getSkillValue(spellNumT skill) const {
    iMax = getMaxSkillValue(skill);
    value = getRawSkillValue(skill);
    value = min(value, iMax);
    value += getSkillApply(skill);  // Equipment/buff modifiers
    value = min(value, MAX_SKILL_LEARNEDNESS);
    value = max(value, 0);
    return value;
}
```

### doesKnowSkill()

Location: `code/code/misc/skills.cc:1441-1447`

Returns true if the player **knows** the skill (maxSkillValue > 0).

```cpp
bool TBeing::doesKnowSkill(spellNumT skill) const {
    return doesKnow(getMaxSkillValue(skill));
}
```

### bSuccess()

Location: `code/code/misc/discipline.cc:1391-1474`

The **primary skill success check**. Returns true if the skill attempt succeeds.

**Three overloads:**
```cpp
bool TBeing::bSuccess(spellNumT spell);                           // Uses getSkillValue
bool TBeing::bSuccess(int ubCompetence, spellNumT spell);         // Custom competence
bool TBeing::bSuccess(int ubCompetence, double dPiety, spellNumT spell); // With piety
```

**Algorithm:**

1. **Log the attempt** for statistics
2. **Check special cases:**
   - Quaffed potion: automatic success
   - Immortal with AUTO_SUCCESS: success/fail based on NOHASSLE flag
3. **Attempt learn-by-doing** (may increment `ubCompetence`)
4. **Check if known:** `ubCompetence <= 0` → automatic failure
5. **Calculate success limit:**
   ```
   limit = getSkillDiffModifier(spell)  // 35-110 based on TASK_*
   limit *= ubCompetence / MAX_SKILL_LEARNEDNESS
   limit *= getStatMod(STAT_FOC)        // Focus stat modifier
   limit *= plotStat(STAT_KAR, 0.9, 1.125, 1.0)  // Karma modifier
   ```
6. **Roll d100:** `if (number(0, 99) < limit)` → success

**Difficulty-to-Base-Limit Mapping:**
| Difficulty | Base Limit |
|------------|------------|
| TASK_TRIVIAL | 110 |
| TASK_EASY | 100 |
| TASK_NORMAL | 90 |
| TASK_DIFFICULT | 80 |
| TASK_DANGEROUS | 70 |
| TASK_HOPELESS | 50 |
| TASK_IMPOSSIBLE | 35 |

At max learnedness (100) with average stats, these translate to approximate success rates:
- Trivial: 100% (110 capped)
- Easy: ~90%
- Normal: ~80%
- Difficult: ~70%
- Dangerous: ~60%

### learnFromDoing()

Location: `code/code/misc/discipline.cc:2863-3120+`

Handles **automatic skill improvement through use**. Called during `bSuccess()`.

**Requirements:**
1. Player must be mortal (not immortal)
2. Skill must have `startLearnDo != -1` configured
3. Player must know the skill
4. Cooldown must have elapsed:
   - Skill ≤ 50%: 30 seconds
   - Skill > 50%: 3 minutes

**Discipline Learning:**
Also has a chance to increase the discipline's `doLearnedness` (separate from skill learning).

**Skill Learning Probability:**
```cpp
amount = (MAX_SKILL_LEARNEDNESS - actual) / MAX_SKILL_LEARNEDNESS;  // 0.0 to 1.0
power = 3.0 - plotStat(STAT_WIS, 1.0, 2.5, 1.75);                   // 0.5 to 2.0
chance = 1000.0 * pow(amount, power);
chance = max(15, chance);  // Minimum 1.5% chance
if (number(0, 999) < chance) → learn
```

**On Success:**
- Skill increases by `amtLearnDo` points (from skill configuration)
- Capped at 100 and at `getMaxSkillValue()`
- Updates `lastUsed` timestamp

## Discipline Training System

### Practice Cost by Discipline Type

| Type | Cost per Point | Description |
|------|----------------|-------------|
| Basic (`isBasic()`) | 1 prac | Class foundation disciplines (Mage, Warrior, etc.) |
| Fast (`isFast()`) | variable (up to 5 pts/prac) | Weapon proficiencies, some advanced discs |
| Standard | 1-3 pracs | Most disciplines |

### CalcRaiseDisc()

Location: `code/code/misc/gaining.cc:153-253`

Calculates **discipline points gained per practice session**:

```cpp
if (disc->isBasic()) return 1;
if (disc->isFast()) return min(5, MAX_DISC_LEARNEDNESS - natLearn);

// Standard disciplines:
if (natLearn >= 100) return 0;
else if (natLearn <= 15) return 3;
else if (natLearn <= 72) return 2;
else return 1;
```

**Standard Discipline Curve:**
- 0-15%: 3 points per practice
- 16-72%: 2 points per practice
- 73-100%: 1 point per practice

Total practices to max a standard discipline: **60 practices**

### raiseDiscOnce()

Location: `code/code/misc/gaining.cc:322-341`

Increases a discipline by one practice worth:
```cpp
void TBeing::raiseDiscOnce(discNumT which) {
    amount = getDiscipline(which)->getNatLearnedness();
    amount += calcRaiseDisc(which, FALSE);
    getDiscipline(which)->setNatLearnedness(min(MAX_DISC_LEARNEDNESS, amount));
    getDiscipline(which)->setLearnedness(min(MAX_DISC_LEARNEDNESS, amount));
    affectTotal();  // Recalculate all derived values
}
```

## Discipline Types

Disciplines with special practice costs:

### Basic Disciplines (`isBasic()` returns true)
- DISC_MAGE, DISC_CLERIC, DISC_WARRIOR, DISC_THIEF, DISC_MONK
- DISC_RANGER, DISC_DEIKHAN, DISC_SHAMAN
- DISC_COMMONER, DISC_COMBAT, DISC_LORE, DISC_THEOLOGY

### Fast Disciplines (`isFast()` returns true)
- DISC_SLASH, DISC_BLUNT, DISC_PIERCE, DISC_BAREHAND (weapon proficiencies)
- DISC_RANGED, DISC_DEFENSE, DISC_OFFENSE
- DISC_ADV_ADVENTURING, DISC_PSIONICS

## Skill Configuration Example

From `spell_info.cc`:

```cpp
discArray[SPELL_GUST] = new spellInfo(
    SPELL_MAGE,           // typ - skill type
    DISC_MAGE,            // disc - primary discipline
    DISC_AIR,             // assDisc - associated (advanced) discipline
    STAT_INT,             // modifierStat
    "gust",               // name
    TASK_NORMAL,          // task - difficulty (90% base at max)
    LAG_1,                // lag
    POSITION_SITTING,     // minPosition
    MANA_10,              // minMana
    LIFEFORCE_0,          // minLifeforce
    PRAY_0,               // minPiety
    TAR_VIOLENT | TAR_FIGHT_VICT | TAR_CHAR_ROOM | TAR_SELF_NONO,  // targets
    SYMBOL_STRESS_0,      // holyStrength
    "", "", "", "",       // fade messages
    START_1,              // start - available at 1% Air discipline
    LEARN_100,            // learn - maxes immediately (100 * 1 = 100)
    START_DO_30,          // startLearnDo - learn-by-doing starts at 30% disc
    LEARN_DO_5,           // amtLearnDo - gains 5 points per learn event
    START_DO_NO,          // secStartLearnDo (unused)
    LEARN_DO_NO,          // secAmtLearnDo (unused)
    LEARN_DIFF_SPELLS,    // learnDoDiff
    0.04,                 // alignMod
    COMP_GESTURAL | COMP_VERBAL | SPELL_TASKED,  // comp_types
    0                     // toggle (quest bit requirement)
);
```

This gust spell:
- Belongs to Air discipline within Mage
- Available at just 1% Air learnedness
- Maxes to 100% as soon as available (LEARN_100)
- Learn-by-doing starts when Air reaches 30%
- Gains 5 skill points per successful learn event
- Has TASK_NORMAL difficulty (~80% success at max)

## Summary Flow

1. **Player practices discipline** at guildmaster
   - `raiseDiscOnce()` called
   - Discipline learnedness increases based on `CalcRaiseDisc()`

2. **Skill becomes available** when `disciplineLearnedness >= skill.start`
   - `getMaxSkillValue()` returns > 0
   - `doesKnowSkill()` returns true

3. **Player uses skill**
   - `bSuccess(spell)` called
   - `learnFromDoing()` may increase skill
   - Success calculated based on learnedness, difficulty, and stats

4. **Skill max increases** as discipline increases
   - Formula: `min(learn * (discLearn - start + 1), 100)`

5. **Learn-by-doing** gradually improves skill through use
   - Probability based on current level and Wisdom
   - Cooldown prevents farming (30s or 3min)
   - Capped at current max from discipline
