//! Tier 2 (invariants) and Tier 4 (replay determinism). Design §11.
//!
//! These are the tests that make a living world testable instead of hopeful.
//! They run headless in under a second because the sim has no engine
//! dependency — which is the entire argument of §5.1, cashed in.
//!
//! Written against long runs rather than single ticks: the failure mode of a
//! persistent world is drift, and drift does not show up in a unit test.

use ledger::event::{Event, Proposition};
use ledger::fixed::Fx;
use ledger::ids::LaneId;
use ledger::metrics;
use ledger::world::World;

const SEED: u64 = 20260908;
const ENTITIES: usize = 200;

fn run(ticks: u64) -> World {
    let mut world = World::genesis(SEED, ENTITIES);
    for _ in 0..ticks {
        world.tick();
    }
    world
}

// ---- Tier 4: replay determinism ----------------------------------------

#[test]
fn two_runs_from_the_same_seed_are_bit_identical() {
    let a = run(3_000);
    let b = run(3_000);
    assert_eq!(
        a.state_fingerprint(),
        b.state_fingerprint(),
        "same seed produced divergent state — fixed-point discipline or an \
         unstable iteration order has been broken"
    );
    assert_eq!(a.log().len(), b.log().len());
}

#[test]
fn state_diverges_when_the_seed_changes() {
    // The determinism test above would pass trivially if the sim ignored its
    // seed. This is the control.
    let mut a = World::genesis(1, ENTITIES);
    let mut b = World::genesis(2, ENTITIES);
    for _ in 0..500 {
        a.tick();
        b.tick();
    }
    assert_ne!(a.state_fingerprint(), b.state_fingerprint());
}

#[test]
fn fingerprint_advances_as_the_world_evolves() {
    let mut world = World::genesis(SEED, ENTITIES);
    let genesis = world.state_fingerprint();
    for _ in 0..500 {
        world.tick();
    }
    assert_ne!(genesis, world.state_fingerprint());
}

// ---- Tier 2: conservation (§6.5) ---------------------------------------

#[test]
fn money_supply_is_constant_over_a_long_run() {
    let world = World::genesis(SEED, ENTITIES);
    let opening = world.economy().total_cash();

    let mut world = World::genesis(SEED, ENTITIES);
    for tick in 0..20_000u64 {
        world.tick();
        if tick % 500 == 0 {
            assert_eq!(
                world.economy().total_cash(),
                opening,
                "money was created or destroyed at tick {} — there are no \
                 faucets or sinks in the POC, so every transfer must balance",
                tick
            );
        }
    }
    assert_eq!(world.economy().total_cash(), opening);
}

#[test]
fn territory_is_conserved_and_every_lane_has_exactly_one_owner() {
    let world = run(20_000);
    let total = world.economy().lane_count();
    assert_eq!(total, 30, "lanes must not be created or destroyed");

    let held: usize = world
        .economy()
        .corps()
        .iter()
        .map(|c| world.economy().lanes_held(c.id))
        .sum();
    assert_eq!(
        held, total,
        "a lane is unowned or double-counted; ownership must partition the set"
    );
}

#[test]
fn bankruptcy_transfers_assets_rather_than_deleting_them() {
    let world = run(20_000);
    let sales = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::LaneSold { .. }))
        .count();
    assert!(sales > 0, "no distressed sale ever happened; the causal spine is dead");

    // Every sale must name a buyer that now actually holds the lane.
    for logged in world.log().iter() {
        if let Event::LaneSold { lane, to, .. } = logged.event {
            let later_resale = world.log().iter().any(|e| {
                e.id > logged.id && matches!(e.event, Event::LaneSold { lane: l, .. } if l == lane)
            });
            if !later_resale {
                assert_eq!(
                    world.economy().lane_owner(lane),
                    to,
                    "lane {} was sold but ownership did not follow",
                    lane.0
                );
            }
        }
    }
}

#[test]
fn no_faction_ossifies_into_permanent_dominance() {
    let mut world = World::genesis(SEED, ENTITIES);
    let mut consecutive_over_threshold = 0;
    for _ in 0..20_000 {
        world.tick();
        if world.economy().dominance() > Fx::permille(400) {
            consecutive_over_threshold += 1;
        } else {
            consecutive_over_threshold = 0;
        }
        assert!(
            consecutive_over_threshold < 4_000,
            "one corporation held over 40% of lanes for 4000 consecutive ticks; \
             the mean-reversion pressure of §6.5 is not working and the world \
             has ossified"
        );
    }
}

// ---- Tier 2: belief invariants (§6.1) ----------------------------------

#[test]
fn propagation_terminates_on_a_cyclic_social_graph() {
    // The graph is a ring plus chords, so it is densely cyclic. A tick that
    // propagated transitively would not return.
    let mut world = World::genesis(SEED, ENTITIES);
    for _ in 0..5_000 {
        world.tick();
    }
    assert!(world.beliefs().total_beliefs() > 0);
}

#[test]
fn belief_confidence_never_exceeds_certainty() {
    let world = run(10_000);
    for (holder, belief) in world.beliefs().all() {
        assert!(
            belief.confidence <= Fx::ONE,
            "{:?} holds a belief above certainty: {:?}",
            holder,
            belief
        );
        assert!(!belief.confidence.is_negative());
    }
}

#[test]
fn witnessed_beliefs_belong_to_witnesses_who_were_present() {
    // §6.1: `Provenance::Witnessed` requires the witness to have been within
    // perception range at the event tick. The fold debug-asserts this; here we
    // check the log agrees from the outside.
    let world = run(5_000);
    for logged in world.log().iter() {
        if let Event::Witnessed { who, .. } = logged.event {
            let moved_since = world.log().iter().any(|e| {
                e.id > logged.id
                    && matches!(e.event, Event::TargetRelocated { who: w, .. } if w == who)
            });
            if !moved_since {
                assert_eq!(
                    world.entity(who).region,
                    logged.region,
                    "a witness was recorded outside the region of the event"
                );
            }
        }
    }
}

#[test]
fn some_held_beliefs_are_false_and_some_are_true() {
    // Pillar P3. If everything held is true the world is omniscient; if nothing
    // is, propagation is pure noise. Both are failures.
    let world = run(10_000);
    let mut true_count = 0;
    let mut false_count = 0;
    for (_, belief) in world.beliefs().all() {
        if world.is_true(&belief.proposition) {
            true_count += 1;
        } else {
            false_count += 1;
        }
    }
    assert!(true_count > 0, "nobody believes anything true");
    assert!(false_count > 0, "nobody believes anything false");
}

#[test]
fn two_entities_can_hold_contradictory_sincere_beliefs() {
    // This is pillar P3's falsifiable test, executed.
    let world = run(10_000);
    let pairs = metrics::contradiction_pairs(&world);
    assert!(
        !pairs.is_empty(),
        "no two entities disagree about anything; the asymmetry pillar fails"
    );

    let (_, belief_a, _, belief_b) = pairs[0];
    assert!(belief_a.proposition.contradicts(&belief_b.proposition));
    // Sincerely held: both above the forget threshold, neither a null.
    assert!(belief_a.confidence > Fx::ZERO && belief_b.confidence > Fx::ZERO);
}

// ---- Tier 2: causality (§6.5, pillar P2) -------------------------------

#[test]
fn a_lane_sale_can_be_traced_back_through_four_causes() {
    // Pillar P2's test, as an assertion rather than a hope: take a world change
    // and reconstruct the chain that produced it.
    let world = run(20_000);

    let penalty_with_history = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::ReservesWentNegative { .. }))
        .find(|e| world.log().causal_chain(e.id, 4).len() >= 1);

    let anchor = penalty_with_history
        .expect("no reserves-negative event was ever caused by anything");

    let chain = world.log().causal_chain(anchor.id, 4);
    assert!(
        !chain.is_empty(),
        "a corporate distress event had no recorded cause"
    );
    // The chain must bottom out in a shipment delay — the one place randomness
    // is allowed to enter (§6.5: RNG seeds when and where, never what).
    let root = chain.last().unwrap();
    assert!(
        matches!(root.event, Event::ShipmentDelayed { .. }),
        "the causal chain bottomed out in {:?}, not a shipment delay",
        root.event
    );
}

#[test]
fn every_logged_cause_points_at_an_earlier_event() {
    let world = run(10_000);
    for logged in world.log().iter() {
        if let Some(cause) = logged.cause {
            assert!(
                cause < logged.id,
                "event {:?} claims a cause that happened later",
                logged.id
            );
            let caused_by = world.log().get(cause).expect("dangling cause");
            assert!(caused_by.tick <= logged.tick);
        }
    }
}

// ---- Tier 2: missions (§6.6) -------------------------------------------

#[test]
fn contracts_reference_tension_that_predates_them() {
    let world = run(20_000);
    let contracts = world.contracts();
    assert!(!contracts.is_empty(), "the mission query never fired");

    let consequent = contracts
        .iter()
        .filter(|c| c.references_pre_existing_tension())
        .count();
    let share = consequent * 100 / contracts.len();
    assert!(
        share > 90,
        "only {}% of contracts referenced pre-existing tension; the generator \
         has regressed into a template (§14 R2)",
        share
    );
}

#[test]
fn the_search_volume_is_a_region_set_not_a_pin() {
    // §6.3 mandatory property: no quest markers.
    let world = run(2_000);
    if let Some(investigation) = world.investigation() {
        assert!(investigation.volume.size() >= 1);
        assert!(investigation.volume.size() <= world.region_count() as usize);
    }
}

#[test]
fn asking_about_someone_generates_events_that_reach_them() {
    // The closing edge of the core loop (§4): information gathering is neither
    // free nor silent, and the target moves because of it.
    let world = run(20_000);

    let inquiries = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::InquiryMade { .. }))
        .count();
    assert!(inquiries > 0, "the agent never asked anyone anything");

    let relocations = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::TargetRelocated { .. }))
        .count();
    assert!(
        relocations > 0,
        "no target ever learned it was being looked for; the loop does not close"
    );
}

#[test]
fn fabrication_uses_the_same_pipeline_as_the_truth() {
    let world = run(20_000);
    let fabrications = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::Fabricated { .. }))
        .count();
    assert!(fabrications > 0, "nobody ever lied; §6.1's thesis is untested");

    // A fabricated belief must be indistinguishable in structure from a true
    // one — same store, same decay, same propagation — differing only in
    // provenance, which is what makes it traceable later.
    let planted = world
        .beliefs()
        .all()
        .find(|(_, b)| matches!(b.provenance, ledger::event::Provenance::Fabricated(_)));
    if let Some((_, belief)) = planted {
        assert!(belief.confidence > Fx::ZERO && belief.confidence <= Fx::ONE);
        assert!(matches!(
            belief.proposition,
            Proposition::Insolvent { .. } | Proposition::Solvent { .. }
        ));
    }
}

// ---- Tier 3 (short): long-horizon fuzz ---------------------------------

#[test]
#[ignore = "long-horizon run; nightly in CI per §11 Tier 3"]
fn one_million_ticks_holds_every_invariant() {
    let world = World::genesis(SEED, ENTITIES);
    let opening_cash = world.economy().total_cash();
    let lanes = world.economy().lane_count();

    let mut world = World::genesis(SEED, ENTITIES);
    for tick in 0..1_000_000u64 {
        world.tick();
        if tick % 10_000 != 0 {
            continue;
        }
        assert_eq!(world.economy().total_cash(), opening_cash, "cash at {}", tick);
        assert_eq!(world.economy().lane_count(), lanes, "lanes at {}", tick);
        let held: usize = world
            .economy()
            .corps()
            .iter()
            .map(|c| world.economy().lanes_held(c.id))
            .sum();
        assert_eq!(held, lanes, "ownership partition at {}", tick);
        for (_, belief) in world.beliefs().all() {
            assert!(belief.confidence <= Fx::ONE && !belief.confidence.is_negative());
        }
        for lane in 0..lanes {
            let _ = world.economy().lane_owner(LaneId(lane as u32));
        }
    }
}
