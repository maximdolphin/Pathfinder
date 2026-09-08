//! The text world-dump. Design §13.1.
//!
//! This file is the Phase 0 deliverable. The gate is not "does the sim run" —
//! it is **"is this a story you would want to read?"**, and this is what gets
//! read. Everything else in the crate exists to make these paragraphs true.
//!
//! Three questions the dump must answer, from §13.1:
//!   1. Can you trace a corporate collapse back four causes?
//!   2. Do two NPCs disagree about the same fact for legible reasons?
//!   3. Did anything surprise you?

use crate::event::{DelayCause, Event, LoggedEvent};
use crate::metrics::{self, Metrics};
use crate::world::World;
use std::fmt::Write;

pub fn render(world: &World, metrics: &Metrics) -> String {
    let mut out = String::new();
    header(&mut out, world);
    collapses(&mut out, world);
    disagreements(&mut out, world);
    the_contract(&mut out, world);
    standings(&mut out, world);
    metrics_table(&mut out, metrics);
    out
}

fn rule(out: &mut String, title: &str) {
    let _ = write!(out, "\n\n{}\n{}\n\n", title, "=".repeat(title.len()));
}

fn header(out: &mut String, world: &World) {
    let _ = write!(
        out,
        "LEDGER — Phase 0 world dump\n\
         seed {}  ·  {} ticks  ·  {} entities  ·  {} regions  ·  {} events logged",
        world.seed(),
        world.now().0,
        world.entities().len(),
        world.region_count(),
        world.log().len(),
    );
}

fn describe_event(world: &World, logged: &LoggedEvent) -> String {
    let corp = |id| world.economy().corp(id).name;
    let who = |id| world.entity(id).name.as_str();
    match logged.event {
        Event::ShipmentDispatched { contract } => {
            let s = world.economy().shipment(contract);
            format!("{} dispatched a shipment to {}", corp(s.shipper), corp(s.client))
        }
        Event::ShipmentDelayed { contract, cause } => {
            let s = world.economy().shipment(contract);
            let reason = match cause {
                DelayCause::LaneCongestion => "the lane was congested",
                DelayCause::OperatorOverhead => "the operator is carrying too much",
                DelayCause::CrewShortage => "there was no crew to load it",
            };
            format!("{}'s shipment ran late — {}", corp(s.shipper), reason)
        }
        Event::ShipmentDelivered { contract } => {
            let s = world.economy().shipment(contract);
            format!("{} delivered on time", corp(s.shipper))
        }
        Event::CashTransferred { from, to, amount } => {
            format!("{} paid {} {}", corp(from), corp(to), amount)
        }
        Event::PenaltyTriggered {
            debtor,
            creditor,
            amount,
            ..
        } => format!(
            "the contract penalty triggered against {} — {} owed to {}",
            corp(debtor),
            amount,
            corp(creditor)
        ),
        Event::ReservesWentNegative { corp: c } => {
            format!("{}'s reserves went negative", corp(c))
        }
        Event::LaneSold {
            lane,
            from,
            to,
            price,
        } => format!(
            "{} sold lane {} to {} for {}",
            corp(from),
            lane.0,
            corp(to),
            price
        ),
        Event::Witnessed { who: w, proposition } => {
            format!("{} saw: {}", who(w), metrics::describe(world, &proposition))
        }
        Event::Told {
            from,
            to,
            proposition,
            confidence,
        } => format!(
            "{} told {}: {} (confidence {})",
            who(from),
            who(to),
            metrics::describe(world, &proposition),
            confidence
        ),
        Event::Fabricated { by, to, proposition } => format!(
            "{} planted a story with {}: {}",
            who(by),
            who(to),
            metrics::describe(world, &proposition)
        ),
        Event::ObligationIncurred {
            debtor,
            creditor,
            class,
            ..
        } => format!("{} came to owe {} {}", who(debtor), who(creditor), class.label()),
        Event::ObligationRedeemed {
            debtor,
            creditor,
            class,
        } => format!(
            "{} called in {} from {}",
            who(creditor),
            class.label(),
            who(debtor)
        ),
        Event::ContractPosted {
            archetype,
            poster,
            target,
            ..
        } => format!(
            "{} posted a {:?} contract on {}",
            corp(poster),
            archetype,
            who(target)
        ),
        Event::InquiryMade {
            asker,
            source,
            about,
        } => format!("{} asked {} about {}", who(asker), who(source), who(about)),
        Event::TargetRelocated { who: w, from, to } => format!(
            "{} left {} for {}",
            who(w),
            world.region_name(from),
            world.region_name(to)
        ),
        Event::ContractResolved { by, alive, .. } => format!(
            "{} closed the contract — target taken {}",
            who(by),
            if alive { "alive" } else { "dead" }
        ),
    }
}

/// Question 1: can you trace a corporate collapse back four causes?
fn collapses(out: &mut String, world: &World) {
    rule(out, "WHAT CHANGED HANDS, AND WHY");

    let sales: Vec<&LoggedEvent> = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::LaneSold { .. }))
        .collect();

    if sales.is_empty() {
        let _ = write!(
            out,
            "No lane changed hands. Either the run was short or nothing went\n\
             wrong enough — both are findings.\n"
        );
        return;
    }

    let _ = write!(
        out,
        "{} lanes changed hands over the run. Three of them, traced backwards:\n",
        sales.len()
    );

    // The event that *caused* a sale is the reserves-negative signal, whose own
    // cause is the delay. Walking the chain is the P2 test.
    for sale in sales.iter().rev().take(3) {
        let _ = write!(out, "\n  {} — {}\n", sale.tick, describe_event(world, sale));
        // A sale's causal parent is not stored on the sale itself (it is a
        // consequence of the whole default chain), so anchor on the penalty
        // that immediately precedes it in the same tick.
        let anchor = world
            .log()
            .iter()
            .filter(|e| e.tick == sale.tick)
            .find(|e| matches!(e.event, Event::ReservesWentNegative { .. }))
            .or_else(|| {
                world
                    .log()
                    .iter()
                    .filter(|e| e.tick == sale.tick)
                    .find(|e| matches!(e.event, Event::PenaltyTriggered { .. }))
            });

        match anchor {
            Some(anchor) => {
                let _ = write!(out, "    because {}\n", describe_event(world, anchor));
                for (depth, cause) in world.log().causal_chain(anchor.id, 3).iter().enumerate() {
                    let _ = write!(
                        out,
                        "    {}because {} ({})\n",
                        "  ".repeat(depth + 1),
                        describe_event(world, cause),
                        cause.tick
                    );
                }
            }
            None => {
                let _ = write!(out, "    (no recorded cause — this is a bug, not a mystery)\n");
            }
        }
    }
}

/// Question 2: do two NPCs disagree about the same fact for legible reasons?
fn disagreements(out: &mut String, world: &World) {
    rule(out, "WHO DISAGREES, AND WHY");

    let pairs = metrics::contradiction_pairs(world);
    if pairs.is_empty() {
        let _ = write!(
            out,
            "Nobody holds a contradictory belief. Pillar P3 fails on this run.\n"
        );
        return;
    }

    let _ = write!(
        out,
        "{} contradictory belief pairs are live. A sample:\n",
        pairs.len()
    );

    for (holder_a, belief_a, holder_b, belief_b) in pairs.iter().take(4) {
        let _ = write!(
            out,
            "\n  {} believes {} (confidence {}, {} hops, {:?})\n  {} believes {} (confidence {}, {} hops, {:?})\n",
            world.entity(*holder_a).name,
            metrics::describe(world, &belief_a.proposition),
            belief_a.confidence,
            belief_a.hops,
            belief_a.provenance,
            world.entity(*holder_b).name,
            metrics::describe(world, &belief_b.proposition),
            belief_b.confidence,
            belief_b.hops,
            belief_b.provenance,
        );
        let truth_a = world.is_true(&belief_a.proposition);
        let truth_b = world.is_true(&belief_b.proposition);
        let _ = write!(
            out,
            "  → the first is {}, the second is {}.\n",
            if truth_a { "true" } else { "false" },
            if truth_b { "true" } else { "false" },
        );
    }
}

/// The investigation trace: did the search behave like an investigation, or
/// like a quest marker?
fn the_contract(out: &mut String, world: &World) {
    rule(out, "THE CONTRACT");

    let posted: Vec<&LoggedEvent> = world
        .log()
        .iter()
        .filter(|e| matches!(e.event, Event::ContractPosted { .. }))
        .collect();

    if posted.is_empty() {
        let _ = write!(out, "No contract was ever posted. The mission query found no tension.\n");
        return;
    }

    let (resolved, failed) = world.resolution_record();
    let _ = write!(
        out,
        "{} contracts posted · {} resolved · {} resolution attempts hit a stale location\n",
        posted.len(),
        resolved,
        failed
    );

    let latest = posted.last().unwrap();
    let _ = write!(out, "\nMost recent: {} — {}\n", latest.tick, describe_event(world, latest));

    if let Event::ContractPosted { target, .. } = latest.event {
        let inquiries: Vec<&LoggedEvent> = world
            .log()
            .iter()
            .filter(|e| matches!(e.event, Event::InquiryMade { about, .. } if about == target))
            .collect();
        let _ = write!(out, "  {} inquiries were made about them.\n", inquiries.len());
        for inquiry in inquiries.iter().rev().take(5).rev() {
            let _ = write!(out, "    {} {}\n", inquiry.tick, describe_event(world, inquiry));
        }

        let moves: Vec<&LoggedEvent> = world
            .log()
            .iter()
            .filter(|e| matches!(e.event, Event::TargetRelocated { who, .. } if who == target))
            .collect();
        if moves.is_empty() {
            let _ = write!(out, "  They never moved.\n");
        } else {
            let _ = write!(
                out,
                "  They moved {} times — the inquiries reached them.\n",
                moves.len()
            );
            for m in moves.iter().rev().take(3).rev() {
                let _ = write!(out, "    {} {}\n", m.tick, describe_event(world, m));
            }
        }
    }

    if let Some(investigation) = world.investigation() {
        let regions: Vec<&str> = investigation
            .volume
            .regions()
            .map(|r| world.region_name(r))
            .collect();
        let _ = write!(
            out,
            "\nOpen search volume: {} of {} regions — {}\n  after {} inquiries.\n",
            investigation.volume.size(),
            world.region_count(),
            regions.join(", "),
            investigation.inquiries,
        );
    }
}

fn standings(out: &mut String, world: &World) {
    rule(out, "STANDINGS");
    let _ = write!(out, "  {:<32} {:>10}  {:>6}\n", "corporation", "cash", "lanes");
    let mut corps: Vec<_> = world.economy().corps().iter().collect();
    corps.sort_by_key(|c| std::cmp::Reverse(c.cash));
    for corp in corps {
        let _ = write!(
            out,
            "  {:<32} {:>10}  {:>6}\n",
            corp.name,
            corp.cash.to_string(),
            world.economy().lanes_held(corp.id)
        );
    }
    let _ = write!(
        out,
        "\n  total cash {} (constant by construction — see the conservation invariant)\n\
           open obligations {}\n",
        world.economy().total_cash(),
        world.ledger().open_count()
    );
}

fn metrics_table(out: &mut String, metrics: &Metrics) {
    rule(out, "METRICS (design §12)");
    for metric in &metrics.entries {
        let _ = write!(
            out,
            "  [{}] {:<38} {:>16}   target {}\n",
            if metric.passing { "PASS" } else { "FAIL" },
            metric.name,
            metric.value,
            metric.target
        );
    }
    let _ = write!(
        out,
        "\n  These are the falsifiable half of the gate. The other half is whether\n\
           the three sections above were worth reading.\n"
    );
}
