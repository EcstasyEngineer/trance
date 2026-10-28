import Lean

/- A small mathematical model of one finite two-child sequential iteration.
   It excludes cadence, offsets, sampled durations, repeat and random selection.
   tick is absorbing after completion: the OWNER does not advance a done child.
   External root advancement has a distinct reset rule in C++.
   This file does not prove correspondence with the C++ implementation. -/
namespace TimingAudit
def finished (cap a b t : Nat) : Prop := cap ≤ t ∨ a + b ≤ t
instance (cap a b t : Nat) : Decidable (finished cap a b t) :=
  inferInstanceAs (Decidable (cap ≤ t ∨ a + b ≤ t))
def tick (cap a b t : Nat) : Nat := if finished cap a b t then t else t + 1
def visible (cap a b t : Nat) : Option Bool :=
  if t < cap ∧ t < a + b then some (decide (a ≤ t)) else none
def run (cap a b : Nat) : Nat → Nat
  | 0 => 0
  | n + 1 => tick cap a b (run cap a b n)

theorem consumed_ticks (cap a b n : Nat) :
    run cap a b n = min n (min cap (a + b)) := by
  induction n with
  | zero => simp [run]
  | succ n ih =>
    simp only [run, ih]
    unfold tick finished
    split <;> omega

theorem completion_iff (cap a b t : Nat) :
    finished cap a b t ↔ min cap (a + b) ≤ t := by
  unfold finished
  omega

theorem no_implicit_restart (cap a b t : Nat) (h : finished cap a b t) :
    tick cap a b t = t := by simp [tick, h]

theorem before_completion_advances (cap a b t : Nat)
    (h : t < min cap (a + b)) : tick cap a b t = t + 1 := by
  have hn : ¬ finished cap a b t := by rw [completion_iff]; omega
  simp [tick, hn]

theorem completed_is_absent (cap a b t : Nat) (h : finished cap a b t) :
    visible cap a b t = none := by
  unfold visible finished at *
  have hn : ¬ (t < cap ∧ t < a + b) := by omega
  simp [hn]

theorem first_child (cap a b t : Nat) (h : t < min cap a) :
    visible cap a b t = some false := by
  have hcap : t < cap := by omega
  have hab : t < a + b := by omega
  have ha : ¬ a ≤ t := by omega
  simp [visible, hcap, hab, ha]

theorem second_child (cap a b t : Nat) (ha : a ≤ t)
    (h : t < min cap (a + b)) : visible cap a b t = some true := by
  have hcap : t < cap := by omega
  have hab : t < a + b := by omega
  simp [visible, hcap, hab, ha]

theorem requested_completion : min 2000 (600 + 600) = 1200 := by decide
theorem requested_tail (t : Nat) (h : 1200 ≤ t) :
    visible 2000 600 600 t = none := by
  apply completed_is_absent
  unfold finished
  omega

#print axioms completion_iff
#print axioms consumed_ticks
#print axioms no_implicit_restart
#print axioms requested_tail
end TimingAudit
