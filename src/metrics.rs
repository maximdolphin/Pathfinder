//! The §12 metrics. None of them is frame rate.
//!
//! These are the falsifiable half of the Phase 0 gate. "Is this a story I would
//! want to read?" is answered by a human reading the dump; these numbers say
//! whether the machinery underneath is actually doing what the design claims.

use crate::belief::Belief;
use crate::event::{Archetype, Proposition};
use crate::fixed::Fx;
use crate::ids::EntityId;
use crate::world::World;

/// A measured value with the band §12 says it must fall in.
pub struct Metric {
    pub name: &'static str,
    pub value: String,
    pub target: &'static str,
    pub passing: bool,
}

pub struct Metrics {
    pub entries: Vec<Metric>,
}

impl Metrics {
    pub fn all_passing(&self) -> bool {
        self.entries.iter().all(|m| m.passing)
    }
}

fn median(values: &mut Vec<u64>) -> u64 {
    if values.is_empty() {
        return 0;
    }
    values.sort_unstable();
    values[values.len() / 2]
}

/// Fraction of held beliefs that are false against ground truth. Below 10% the
/// asymmetry pillar (P3) is dead — everyone effectively knows everything.
pub fn false_belief_share(world: &World) -> Fx {
    let mut total = 0i64;
    let mut false_count = 0i64;
    for (_, belief) in world.beliefs().all() {
        total += 1;
        if !world.is_true(&belief.proposition) {
            false_count += 1;
        }
    }
    if total == 0 {
        return Fx::ZERO;
    }
    Fx::from_int(false_count).div(Fx::from_int(total))
}

pub fn median_propagation_depth(world: &World) -> u64 {
    let mut hops: Vec<u64> = world
        .beliefs()
        .all()
        .filter(|(_, b)| b.hops > 0)
        .map(|(_, b)| b.hops as u64)
        .collect();
    median(&mut hops)
}

/// Two entities holding contradictory, sincerely-held beliefs about the same
/// fact. This is pillar P3's test, counted rather than asserted.
pub fn contradiction_pairs(world: &World) -> Vec<(EntityId, Belief, EntityId, Belief)> {
    let held: Vec<(EntityId, Belief)> = world.beliefs().all().map(|(e, b)| (e, *b)).collect();
    let mut pairs = Vec::new();
    for (i, (holder_a, belief_a)) in held.iter().enumerate() {
        for (holder_b, belief_b) in held.iter().skip(i + 1) {
            if holder_a == holder_b {
                continue;
            }
            if belief_a.proposition.contradicts(&belief_b.proposition) {
                pairs.push((*holder_a, *belief_a, *holder_b, *belief_b));
            }
        }
    }
    pairs
}

pub fn archetype_counts(world: &World) -> Vec<(Archetype, usize)> {
    let mut counts = vec![
        (Archetype::Bounty, 0usize),
        (Archetype::Audit, 0),
        (Archetype::Collection, 0),
    ];
    for contract in world.contracts() {
        for entry in counts.iter_mut() {
            if entry.0 == contract.archetype {
                entry.1 += 1;
            }
        }
    }
    counts
}

fn percent(value: Fx) -> String {
    format!("{}%", value.mul(Fx::from_int(100)).trunc_int())
}

pub fn collect(world: &World, tick_cost_micros: u128) -> Metrics {
    let mut entries = Vec::new();

    entries.push(Metric {
        name: "Per-region tick cost",
        value: format!("{} us", tick_cost_micros),
        target: "< 5000 us",
        passing: tick_cost_micros < 5000,
    });

    let depth = median_propagation_depth(world);
    entries.push(Metric {
        name: "Belief propagation depth (median)",
        value: format!("{} hops", depth),
        target: "3-6 hops",
        passing: (3..=6).contains(&depth),
    });

    let false_share = false_belief_share(world);
    let false_pct = false_share.mul(Fx::from_int(100)).trunc_int();
    entries.push(Metric {
        name: "Beliefs held that are false",
        value: percent(false_share),
        target: "10-25%",
        passing: (10..=25).contains(&false_pct),
    });

    let counts = archetype_counts(world);
    let total: usize = counts.iter().map(|c| c.1).sum();
    let largest = counts.iter().map(|c| c.1).max().unwrap_or(0);
    let largest_pct = if total == 0 {
        0
    } else {
        (largest * 100 / total) as i64
    };
    // §12's 35% bound is written for the six archetypes of §6.6. The POC
    // implements three, where an even split is already 33% — so the literal
    // bound leaves under two points of headroom and is far tighter than the
    // metric was ever meant to be. Reported as specified anyway: moving a
    // target because the run missed it is how a gate stops being a gate.
    let archetypes_implemented = counts.len();
    let even_share = 100 / archetypes_implemented.max(1);
    entries.push(Metric {
        name: "Largest mission archetype share",
        value: format!(
            "{}% of {} ({} archetypes, even = {}%)",
            largest_pct, total, archetypes_implemented, even_share
        ),
        target: "< 35% (oatmeal detector)",
        passing: total > 0 && largest_pct < 35,
    });

    let consequent = world
        .contracts()
        .iter()
        .filter(|c| c.references_pre_existing_tension())
        .count();
    let consequent_pct = if total == 0 {
        0
    } else {
        (consequent * 100 / total) as i64
    };
    entries.push(Metric {
        name: "Contracts from pre-existing tension",
        value: format!("{}%", consequent_pct),
        target: "> 90%",
        passing: total > 0 && consequent_pct > 90,
    });

    let redemption = world.ledger().redemption_rate();
    let redemption_pct = redemption.mul(Fx::from_int(100)).trunc_int();
    entries.push(Metric {
        name: "Favour redemption rate",
        value: percent(redemption),
        target: "40-70%",
        passing: (40..=70).contains(&redemption_pct),
    });

    let gini = world.economy().lane_gini();
    entries.push(Metric {
        name: "Lane-holding Gini",
        value: gini.to_string(),
        target: "bounded, oscillating",
        passing: gini < Fx::permille(600),
    });

    let dominance = world.economy().dominance();
    entries.push(Metric {
        name: "Peak faction dominance",
        value: percent(dominance),
        target: "<= 40%",
        // §6.5 forbids *exceeding* 40%. Sitting at the acquisition cap is the
        // system working, not failing.
        passing: dominance <= Fx::permille(400),
    });

    entries.push(Metric {
        name: "Contradictory belief pairs",
        value: format!("{}", contradiction_pairs(world).len()),
        target: "> 0 (pillar P3)",
        passing: !contradiction_pairs(world).is_empty(),
    });

    Metrics { entries }
}

/// Cheap sanity check used by the harness: propositions we can still name.
pub fn describe(world: &World, proposition: &Proposition) -> String {
    match *proposition {
        Proposition::LocatedIn { who, region } => format!(
            "{} is in {}",
            world.entity(who).name,
            world.region_name(region)
        ),
        Proposition::Solvent { corp } => {
            format!("{} is solvent", world.economy().corp(corp).name)
        }
        Proposition::Insolvent { corp } => {
            format!("{} is insolvent", world.economy().corp(corp).name)
        }
        Proposition::ControlsLane { corp, lane } => format!(
            "{} controls lane {}",
            world.economy().corp(corp).name,
            lane.0
        ),
        Proposition::Skimming { who, from } => format!(
            "{} is skimming from {}",
            world.entity(who).name,
            world.economy().corp(from).name
        ),
        Proposition::BountyPosted { on, by } => format!(
            "{} has {} looking for them",
            world.entity(on).name,
            world.economy().corp(by).name
        ),
        Proposition::OwesObligation { debtor, creditor } => format!(
            "{} owes {}",
            world.entity(debtor).name,
            world.entity(creditor).name
        ),
    }
}
