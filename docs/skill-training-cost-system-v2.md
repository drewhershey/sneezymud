# Skill Training System v2: Effectiveness-Based Learning

This document outlines a redesigned approach to skill progression that eliminates learn-by-doing (LBD) as a barrier to skill success while preserving it as a path to improved effectiveness.

## Background & Motivation

### The Problem with v1

The original cost-curve system (v1) attempted to make the LBD grind *cheaper to skip* for early skill levels. While mathematically sound, it still treated LBD as an obstacle to overcome rather than addressing the root issue: **skills shouldn't fail constantly just because you haven't ground them enough.**

Additionally:
- New players without gold reserves would still face the grind
- The curve math, while elegant, added complexity
- The fundamental frustration remained: "I know this skill, why does it keep failing?"

### The v2 Philosophy

**Separate "can you do it?" from "how well do you do it?"**

- **Discipline investment** determines skill success reliability
- **Skill learnedness** determines skill effectiveness (damage, duration, etc.)

This means:
1. Skills work reliably as soon as you unlock them via discipline
2. LBD improves *how effective* those skills are, not *whether they work*
3. Grinding becomes optional optimization, not mandatory functionality

## Core Design Changes

### Learn-by-Doing Simplification

**Current system:** Each skill has individual configuration for:
- `startLearnDo` (START_DO_*): Discipline % required before LBD begins (ranges from 1 to 60)
- `amtLearnDo` (LEARN_DO_*): Points gained per hone (ranges from 1 to 10)

This complexity existed to control grind pacing. With success now tied to discipline investment, this complexity serves no purpose.

**New system:** All skills use uniform LBD settings:
- LBD starts at 1% learnedness for all skills
- All skills gain the same amount per hone (e.g., 1 point)

**Benefits:**
- Predictable progression across all skills
- Simpler code (can potentially remove START_DO_*/LEARN_DO_* enums entirely)
- No "why can't I improve this skill yet?" confusion
- Easier to balance effectiveness scaling when all skills progress identically

**Implementation:** Either:
1. Set all skills to `START_DO_1, LEARN_DO_1` in `spell_info.cc`, or
2. Modify `learnFromDoing()` to ignore the per-skill config and use constants

### Success Calculation

**Current system:**
```
bSuccess() uses getSkillValue() (actual learnedness from LBD)
```

**New system:**
```
bSuccess() uses getMaxSkillValue() (potential from discipline investment)
```

This is a one-line change that immediately eliminates LBD as a success gate.

### What Learnedness Becomes

Skill learnedness (0-100%) becomes purely an **effectiveness multiplier**:

| Skill Type | Effectiveness Scaling |
|------------|----------------------|
| Damage spells/skills | Damage output |
| Healing | Healing amount |
| Duration buffs/debuffs | Effect duration |
| Stat modifiers | Modifier magnitude |
| Level-capped effects | Maximum affected level |
| Special effects | Chance for bonus effects |

### Example: Backstab

**Before (current system):**
- 30% learnedness → ~30% success rate, full damage when it works
- 100% learnedness → ~80% success rate, full damage when it works

**After (v2 system):**
- 30% learnedness → ~80% success rate (based on max potential), 75-85% damage
- 100% learnedness → ~80% success rate, 100% damage (or higher with bonuses)

The skill *works* from the start. Grinding improves *how much* it does.

## Implementation Phases

### Phase 1: Remove LBD as Success Gate (Immediate)

**Scope:** Single function change + testing

**Change in `discipline.cc`:**
```cpp
// Current
bool TBeing::bSuccess(spellNumT spell) {
  return bSuccess(getSkillValue(spell), spell);
}

// New
bool TBeing::bSuccess(spellNumT spell) {
  return bSuccess(getMaxSkillValue(spell), spell);
}
```

**Impact:**
- All skills immediately become reliable based on discipline investment
- No other code changes required for basic functionality
- Existing LBD system continues to work (just doesn't affect success anymore)
- Players experience immediate quality-of-life improvement

**What still works:**
- LBD still fires and increases learnedness
- Skill displays still show current vs max learnedness
- All existing skill code continues to function

**What changes:**
- Success rates improve dramatically for un-honed skills
- The "grind to make skills work" loop is broken

### Phase 2: Add Effectiveness Scaling (Incremental)

**Scope:** Per-skill modifications over time

#### Step 2a: Centralized Damage/Healing Scaling

Add effectiveness multiplier to the damage calculation pipeline:

```cpp
// New helper function
double TBeing::getSkillEffectiveness(spellNumT skill) const {
    int learn = getRawNatSkillValue(skill);
    // Returns 0.75 at 0% learnedness, 1.0 at 100%
    // Adjust floor/ceiling as balance requires
    return 0.75 + (0.25 * learn / 100.0);
}

// Modify getSkillDam() to apply effectiveness
int TBeing::getSkillDam(...) {
    // ... existing calculation ...
    dam = (int)(dam * getSkillEffectiveness(skill));
    return dam;
}
```

This single change would give all damage and healing skills effectiveness scaling.

**Tuning considerations:**
- Floor (0% learnedness): 75%? 80%? 50%?
- Ceiling (100% learnedness): 100%? 110%? 125%?
- Linear vs curved scaling?

#### Step 2b: Duration Scaling

For buff/debuff spells, add duration multiplier:

```cpp
// In individual spell implementations
aff.duration = baseDuration * caster->getSkillEffectiveness(spell);
```

Or create a centralized helper:
```cpp
int TBeing::getEffectiveDuration(spellNumT skill, int baseDuration) const {
    return (int)(baseDuration * getSkillEffectiveness(skill));
}
```

#### Step 2c: Per-Skill Special Effects

Some skills may benefit from unique effectiveness bonuses:

| Skill | Effectiveness Bonus at 100% |
|-------|----------------------------|
| Backstab | +X% crit chance |
| Fireball | Chance to ignite target |
| Sanctuary | +X% damage reduction |
| Heal | Chance to remove a debuff |
| Bash | +X rounds of lag on target |

These require individual skill modifications and can be added gradually.

#### Step 2d: Skills Without Obvious Scaling

Some skills are binary (detection, movement, utility). Options:
1. **Leave at baseline** - No effectiveness scaling, skill works at full power once unlocked
2. **Add duration** - Detection lasts longer at higher learnedness
3. **Add secondary effects** - Teleport has reduced lag, Identify reveals more info
4. **Reduced resource cost** - Lower mana/moves at higher learnedness

## Pay-to-Train System

The existing gold-for-training system remains valuable but becomes simpler:

### Changes from v1

| Aspect | v1 | v2 |
|--------|----|----|
| What you're buying | Skip grind to make skills work | Skip grind for extra effectiveness |
| Cost curve | Exponential (cheap early, expensive late) | Linear (consistent cost per point) |
| Urgency | High (skills unreliable without it) | Low (skills work fine, this is optimization) |
| Target audience | Everyone (especially new players) | Veterans optimizing characters |

### Simplified Cost Formula

With the grind removed from success, there's no need for a curve that makes early training cheap. Linear pricing is simpler and appropriate:

```
cost = (targetLearnedness - currentLearnedness) * costPerPoint * skillMultiplier
```

Where:
- `costPerPoint` is a global tweak (e.g., 10,000 talens)
- `skillMultiplier` accounts for difficulty, advanced disc, etc.

### Optional: Threshold-Based Training

Could restrict pay-to-train to only work above a certain learnedness:
- "I can only help you refine skills you've already practiced somewhat"
- Requires 25% or 50% natural learnedness before training is available
- Encourages at least *some* organic skill use

## Migration & Compatibility

### Existing Characters

- Characters with high learnedness keep their effectiveness advantage
- Characters with low learnedness immediately gain reliable skill success
- No data migration required - existing values are valid in new system

### Display Changes

Update skill display to clarify the new semantics:

```
Backstab:
  Success: Reliable (85% potential)
  Effectiveness: 67% (room to improve)
```

Or simpler:
```
Backstab: 67% effective (max potential: 85%)
```

## Summary

| Aspect | Current System | v2 System |
|--------|---------------|-----------|
| Skill success based on | Actual learnedness (LBD) | Max potential (discipline) |
| LBD affects | Whether skills work | How effective skills are |
| LBD start threshold | Varies per skill (1-60% disc) | Always 1% learnedness |
| LBD increment | Varies per skill (1-10 pts) | Uniform (1 pt per hone) |
| New skill experience | Frustrating failures | Works immediately, improves over time |
| Grind requirement | Mandatory for functionality | Optional for optimization |
| Pay-to-train purpose | Skip mandatory grind | Purchase extra effectiveness |

### Phase 1 Deliverables
- [ ] Change `bSuccess()` to use `getMaxSkillValue()`
- [ ] Normalize LBD: all skills start at 1%, gain 1 point per hone
- [ ] Update skill display text (optional)
- [ ] Player communication about the change

### Phase 2 Deliverables (Incremental)
- [ ] Add `getSkillEffectiveness()` helper
- [ ] Apply to damage calculation (`getSkillDam()`)
- [ ] Apply to healing calculation
- [ ] Audit duration-based spells
- [ ] Audit modifier-based spells
- [ ] Update pay-to-train for linear costs
- [ ] Remove 100% cap in `getSkillValue()` for effectiveness calculation
- [ ] Implement diminishing returns for effectiveness >100%
- [ ] Review existing +skill equipment for balance
- [ ] Design probability curve for specialist effects
- [ ] Implement `getSpecialistEffectChance()` and `rollSpecialistEffect()`
- [ ] Create data-driven specialist effect configuration
- [ ] Design and implement specialist effects for key skills
- [ ] Add player messaging for specialist effect activation

## Equipment Skill Bonuses

### Current System

Equipment can provide bonuses to skill learnedness via `skillApplyData`:
- Each piece of equipment can modify one or more skills
- Bonuses are applied in `getSkillValue()` after calculating natural learnedness
- Currently capped at `MAX_SKILL_LEARNEDNESS` (100)

```cpp
// In getSkillValue()
value += applyAmt;  // Equipment/buff modifiers
value = min(value, (int)MAX_SKILL_LEARNEDNESS);  // Capped at 100
```

### The Problem

With the current cap at 100%, +skill equipment becomes **worthless at endgame**:
- A player with 95% natural learnedness and +10 backstab gear still has 100% effective
- A player with 100% natural learnedness gets zero benefit from +skill equipment
- This makes +skill gear only useful during the leveling/honing phase

### The v2 Opportunity

If effectiveness can scale **past 100%**, +skill equipment becomes valuable at endgame:

| Natural Learnedness | +Skill Gear | Effective Value | Effectiveness |
|---------------------|-------------|-----------------|---------------|
| 100% | +0 | 100% | 100% |
| 100% | +10 | 110% | ~102.5% damage |
| 100% | +20 | 120% | ~105% damage |

### Implementation Considerations

1. **Remove the cap in getSkillValue()** (for effectiveness calculation only)
   - Success calculation still uses `getMaxSkillValue()` which has its own logic
   - Only effectiveness and specialist effect probability would benefit from values >100%

2. **Diminishing returns for effectiveness**
   - Exact formula TBD based on balance research
   - Key principle: each point above 100% provides progressively smaller effectiveness gains
   - Prevents +skill stacking from being mandatory while still rewarding it

3. **Probability-based specialist effects**
   - Each point above 100% increases *chance* of specialist effects activating
   - No hard thresholds - every point of +skill has value
   - Different effects can have different probability curves
   - Maximum activation chances should be capped to prevent effects from being guaranteed

4. **New itemization opportunities**
   - Endgame gear can include meaningful +skill bonuses
   - Creates trade-offs: +skill vs +dam vs +hit vs defensive stats
   - Specialist effects should complement raw stats, not replace them as optimal choice

5. **Balance considerations**
   - Specialist effects should be utility-focused (debuffs, procs) rather than pure damage multipliers
   - This keeps stat-based builds competitive with specialist builds
   - PvP implications need careful consideration

### Specialist Effects System

Rather than just providing marginal numerical gains, skill values above 100% unlock **specialist effects** - unique bonuses that reward players who invest in mastering specific skills.

#### Design Philosophy

**Probability-based scaling, not hard thresholds:**

Instead of "reach 140% to unlock Expert tier", each point above 100% increases the *probability* of specialist effects activating. This means:
- Every single point of +skill has value
- No "breakpoints" where you optimize to hit exact thresholds
- No feeling of waste when you're "just under" a tier
- Flexible gearing - any amount of +skill provides proportional benefit
- Gradual power growth rather than sudden jumps

**Complementary to stats, not replacing them:**

Specialist effects should be compelling but not so powerful that +skill becomes the obvious optimal choice over +str/+dex/+dam/+hit. The goal is to create *one viable path* among several, enabling build diversity rather than a new meta.

#### How It Works

Each skill can have one or more specialist effects, each with its own probability curve:

```
Effect activation chance = f(skill_value - 100)
```

Where `f` is a diminishing returns function that:
- Returns 0% at 100 skill value (no bonus below full learnedness)
- Increases with each point above 100
- Approaches but never reaches a reasonable cap
- Has diminishing returns to prevent stacking from being mandatory

**Example concept (not final numbers):**
- At 100%: No specialist effect chance
- At 110%: Low chance of minor effect
- At 130%: Moderate chance of minor effect, low chance of major effect
- At 160%+: Good chance of minor effect, moderate chance of major effect

#### Example Specialist Effects by Skill Category

**Combat Skills (backstab, bash, kick, etc.):**
- Minor: Chance to apply a debuff (slow, off-balance)
- Major: Reduced recovery time when effect procs

**Weapon Proficiencies (slash, pierce, bludgeon, etc.):**
- Minor: Increased critical hit chance
- Major: Critical hits apply bonus effects

**Offensive Spells:**
- Minor: Mana cost reduction
- Major: Chance to not trigger cooldown

**Healing/Buff Spells:**
- Minor: Duration extension
- Major: Chance for enhanced potency

**Utility Skills (sneak, hide, pick lock, etc.):**
- Minor: Faster execution
- Major: Chance to bypass normally impossible obstacles

#### Gearing Trade-offs

A player pursuing specialist bonuses faces real opportunity costs:

| +Skill Gear | What You Gain | What You Give Up |
|-------------|---------------|------------------|
| Weapon | Specialist effect chance | Raw +damage, +hit |
| Armor | Specialist effect chance | Defensive stats, resists |
| Jewelry | Specialist effect chance | Stat bonuses, regen |

This creates meaningful build choices:
- **Generalist:** Balanced stats, reliable across situations
- **Specialist:** Higher chance of skill procs, but less raw power/survivability
- **Hybrid:** Some +skill on key pieces, stats elsewhere

#### Implementation Approach

```cpp
// Calculate probability of a specialist effect activating
double TBeing::getSpecialistEffectChance(spellNumT skill, SpecialistEffectType effect) const {
    int learn = getSkillValue(skill);  // Uncapped, includes equipment
    if (learn <= 100) return 0.0;

    // Each effect type can have its own probability curve
    // Diminishing returns formula TBD based on balance research
    return calculateProbability(learn - 100, effect);
}

// Check if a specialist effect activates this use
bool TBeing::rollSpecialistEffect(spellNumT skill, SpecialistEffectType effect) const {
    double chance = getSpecialistEffectChance(skill, effect);
    return ::number(0, 99) < (chance * 100);
}
```

#### Data-Driven Effect Configuration

```cpp
struct SpecialistEffect {
    spellNumT skill;
    SpecialistEffectType type;    // DEBUFF, CRIT_BONUS, COST_REDUCTION, etc.
    int curveParameter;           // Controls how quickly probability scales
    int maxProbability;           // Cap on activation chance
    sstring description;          // Player-visible description
};
```

This allows designers to tune individual effects without code changes.

#### Player Feedback

Subtle messaging when effects activate:

```
Your expertise with backstab leaves your opponent staggered!
```

In skill display, show current bonus:

```
Backstab: 127% effectiveness
  Specialist effect: [X]% chance to stagger target
```

## Open Questions

1. **Uniform hone amount:** How many points per hone? 1 point means 100 hones to max (more gradual). 2 points means 50 hones (faster). Current skills range from 1-10.

2. **Effectiveness floor:** What should 0% learnedness give? 75%? 80%? 50%?

3. **Effectiveness ceiling:** Should 100% learnedness give exactly current values, or allow exceeding them (105%, 110%)?

4. **Scaling curve:** Linear from floor to ceiling, or some curve?

5. **Binary skills:** Leave at full power, or find creative effectiveness bonuses?

6. **Pay-to-train threshold:** Allow training from 0%, or require some organic progress first?

7. **Communication:** How do we explain this change to players?

8. **Equipment scaling:** What diminishing returns formula for effectiveness >100%? What hard cap?

9. **Specialist probability curve:** What function for specialist effect activation? How steeply should probability increase with each point? What's the maximum activation chance?

10. **Effect balance:** How powerful can specialist effects be before they overshadow stat-based builds? Should effects be utility-focused (debuffs, procs) rather than raw damage increases?

11. **Per-skill effects:** Which skills get specialist effects? Do all skills need them, or just combat/spellcasting? What effects make sense for utility skills?

12. **+Skill availability:** How much +skill equipment currently exists in the game? Do we need to create new items to make specialist builds achievable?
