// Included inside pattern::Node only by explicitly opted-in historical tests.
// Never part of the product AST.
enum class Type { Action, Seq, Par, One, Rep, Off, Phase, Burst, Occurrence };
Type type = Type::Occurrence;
    // Rate divider: run the effects only every Nth time the leaf fires (1 = always).
    // Bounded named state owned by the compiled action, not a user variable.
    uint32_t divide = 1;

    // Rep: repetitions in `count`. Off: offset frames in `count`. Both lower
    // children[0].
    uint32_t count = 0;

    // Phase created by `pattern ... seq`: expose its ordered body's active index.
    // Other occurrences retain index 0; they do not inherit a child's burst state.
    bool sequence = false;

    // Phase: owns a bounded activation clock, entry effects and child schedules.
    // Burst owns children[0] (base phase) and children[1] (sampled-duration phase).
    // Its length is the controller's enclosing span; duration/cooldown are period ticks.
    uint32_t burst_period = 0;
    uint32_t burst_chance_den = 0;
    uint32_t burst_cooldown = 0;
    uint32_t burst_dur_min = 0;
    uint32_t burst_dur_max = 0;

