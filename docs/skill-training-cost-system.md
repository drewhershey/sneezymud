## Skill Training Cost System

I have a new system already mostly-coded that allows players to pay gold to train skills directly, rather than relying solely on the existing hone-based system, but I'm looking for feedback on the design, particularly around cost tuning.

Note that this discussion is about the **cost calculation** for skill training, not the actual implementation of the training process itself.

### Background & Goals

Sneezy has a longstanding "learn-by-doing" system where:

1. Players invest practices in disciplines to unlock skills and raise their **maximum potential**
2. Players must then actively use skills to earn random "hones" that increase **actual learnedness**
3. Actual learnedness determines skill success rates

This system is overly punishing and simply not enjoyable in its current form. It takes a lot of failures before skills become reliable and every new character has to go through the same grind of honing skills up, which many players accomplish while AFK via triggers anyway.

I'm adding a new pay-for-training system to help alleviate this frustrating grind, while also adding a much-needed way to remove money from circulation.

### How Cost Is Calculated

**Step 1: Determine the skill's total cost (0→100%)**

```
Total Cost = 1,000,000 × SkillTrainingCost tweak × advanced disc multiplier × skill difficulty multiplier
```

Current skill multipliers:

- **Advanced discipline skills:** ×1.5
- **Skill difficulty:** ×(1 + difficulty × 0.05) - e.g., difficulty 2 = ×1.10
  - Skills difficulty progression goes from 0 to 6: trivial (0), easy, normal, difficult, dangerous, hopeless, impossible (6)
  - Current skill difficulty distribution:
    - Trivial: 31
    - Easy: 199
    - Normal: 207
    - Difficult: 18
    - Dangerous: 10

**Step 2: Distribute cost across hones using a curve**

Each skill has a learning increment (typically 1-3% per hone). The curve determines how much each hone costs relative to others.

### The Curve System

We use an exponential curve centered at a configurable midpoint (default 60%):

- **Early training (0-60%):** Cheaper than average
- **Late training (60-100%):** More expensive than average

A steeper curve means players can affordably train skills to "good enough" levels (where they're reliable), but maxing out becomes a significant gold investment. This supports the design goal of reducing grind while adding a potential gold sink.

**Curve steepness options:**

| Value | Effect                      | Last÷First Ratio |
| ----- | --------------------------- | ---------------- |
| 0     | Flat (all hones same price) | 1×               |
| 1     | Gentle curve                | ~2.7×            |
| 2     | Steep curve (default)       | ~7.4×            |
| 3     | Very steep (maximum)        | ~20×             |

**Critical property:** The curve only redistributes cost—total cost from 0→100% stays the same regardless of curve or midpoint settings.

### Global Tweaks

Three global tweaks, modifiable at-will by imms in-game, control the system:

| Tweak                         | Default | What It Does                        |
| ----------------------------- | ------- | ----------------------------------- |
| **SkillTrainingCost**         | 1.0     | Global cost multiplier              |
| **SkillTrainingCostCurve**    | 2.0     | Curve steepness (0-3)               |
| **SkillTrainingCostMidpoint** | 0.6     | Curve midpoint (0-1, where 0.6=60%) |

### The Math Behind Cost-Per-Hone

Here's the way I'm currently calculating the cost for each hone using the curve. This was conceived by AI, as I'm not mathematically inclined enough to devise something like this on my own, so please feel free to critique or suggest alternatives.

**Weight function for each hone:**

```
weight(p) = e^(k × (p - m))
```

Where:

- `p` = progress point (0 to 1), calculated as the midpoint of the hone's range divided by 100
- `k` = curve steepness (SkillTrainingCostCurve tweak, 0-3)
- `m` = curve midpoint (SkillTrainingCostMidpoint tweak, 0-1)
- `e` = Euler's number (~2.718)

**Examples at different progress points (with k=2, m=0.6):**

- At 1% progress: weight = e^(2 × -0.59) ≈ 0.31 (69% discount)
- At 60% progress: weight = e^(2 × 0) = 1.0 (baseline)
- At 99% progress: weight = e^(2 × 0.39) ≈ 2.18 (118% premium)

**Normalization:**

To ensure the total cost from 0→100% remains constant regardless of curve settings, we normalize:

```
normalization factor = (total hones from 0→100%) / (sum of all weights from 0→100%)
```

**Final cost for a hone:**

```
hone cost = (total skill cost / total hones) × weight(p) × normalization factor
```

**In plain English:** Each hone's cost is the "average cost per hone" adjusted by a multiplier that depends on where you are in the training progression. Early hones get a discount, late hones get a premium, and the normalization ensures these balance out to the same total.

### Worked Example

Training **backstab** (normal difficulty, basic discipline skill, increases by 2% per hone) from 1% to 60%:

**Skill baseline:**

- Total cost 0→100%: 1,000,000 × 1.10 (difficulty) = **1,100,000 talens**
- Total hones: 50 (at 2% each)
- Flat cost per hone (if curve=0): 22,000 talens

**Individual hone costs with default curve (k=2, midpoint=60%):**

| Hone Range | Progress | Weight | Cost per Hone |
| ---------- | -------- | ------ | ------------- |
| 0→2%       | 1%       | 0.31   | ~6,800        |
| 28→30%     | 29%      | 0.54   | ~11,900       |
| 58→60%     | 59%      | 0.98   | ~21,600       |
| 78→80%     | 79%      | 1.47   | ~32,300       |
| 98→100%    | 99%      | 2.18   | ~48,000       |

**Cost distribution:**

| Training Range | % of Total Cost | Approximate Cost |
| -------------- | --------------- | ---------------- |
| 0→60%          | ~36%            | ~396,000 talens  |
| 60→100%        | ~64%            | ~704,000 talens  |

**For the 1→60% training:**

- 30 hones in the discount zone
- **Cost: ~390,000 talens** (vs ~660,000 if flat)
- **Savings: ~41%** compared to flat pricing

**Takeaway:** With the default settings, getting backstab to a reliable 60% costs only ~35% of the total skill cost. The final 40% to max costs nearly twice as much as the first 60%.

## Design Questions

### 1. What's a reasonable base total cost for 0→100% for a single skill?

The base is currently **1,000,000 talens** (before multipliers). Is this appropriate given:

- Typical gold income at various player levels?
- How many skills a class has?
- The intended role as a gold sink?

### 2. Are the curve defaults appropriate?

Current defaults: k=2 (steep curve), midpoint=60%

This means:

- Training to 60% costs ~36% of total
- Training 60→100% costs ~64% of total
- First hone is ~7× cheaper than last hone

Should the curve be steeper/gentler? Should the midpoint be higher/lower?

### 3. What should the skill multipliers be (if any)?

**Advanced discipline skills** (currently ×1.5):

- Does it make sense for advanced skills to cost more?

**Skill difficulty tiers** (currently +5% per tier):

- Does it make sense to charge more for higher-difficulty skills?
- Is a flat +5% per tier appropriate, or should the increments be larger/smaller?
- Should the increments be non-linear (e.g., larger increases for higher difficulties)?

### 4. Should bulk training offer discounts?

The system allows training entire disciplines, all class skills, or even all trainable skills at once. Should these bulk options provide a discount compared to training skills individually?

### 5. Does the math check out and accomplish the design goals?

### 6. Anything else I'm missing?
