//! Events and propositions — the only write path into world state.
//!
//! Design §5.3: `State_n = fold(reduce, State_0, events[0..n])`. Every state
//! mutation is an `Event`. Nothing else may mutate `World`. This is enforced
//! by keeping `World`'s fields private to its module and exposing exactly one
//! mutating method, `World::apply`.
//!
//! Every logged event carries the id of the event that caused it. That single
//! field is what makes pillar P2 testable: given any world change you can walk
//! the chain backwards and read the four causes that produced it.

use crate::fixed::Fx;
use crate::ids::{ContractId, CorpId, EntityId, EventId, LaneId, RegionId, Tick};
use crate::obligation::ObligationClass;

/// A proposition an entity can hold a belief about. Design §6.1: **typed enum,
/// never a string.** Two entities holding contradictory beliefs is expressed by
/// them holding propositions that `contradicts` reports as incompatible.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Proposition {
    LocatedIn { who: EntityId, region: RegionId },
    Solvent { corp: CorpId },
    Insolvent { corp: CorpId },
    ControlsLane { corp: CorpId, lane: LaneId },
    Skimming { who: EntityId, from: CorpId },
    BountyPosted { on: EntityId, by: CorpId },
    OwesObligation { debtor: EntityId, creditor: EntityId },
}

impl Proposition {
    /// Two propositions contradict when they make incompatible claims about the
    /// same subject. Used by §6.3 to widen a search volume rather than silently
    /// picking a winner.
    pub fn contradicts(&self, other: &Proposition) -> bool {
        use Proposition::*;
        match (self, other) {
            (LocatedIn { who: a, region: ra }, LocatedIn { who: b, region: rb }) => {
                a == b && ra != rb
            }
            (Solvent { corp: a }, Insolvent { corp: b }) => a == b,
            (Insolvent { corp: a }, Solvent { corp: b }) => a == b,
            (
                ControlsLane { corp: a, lane: la },
                ControlsLane { corp: b, lane: lb },
            ) => la == lb && a != b,
            _ => false,
        }
    }

    /// The entity a proposition is *about*, when it is about an entity at all.
    /// Corporate propositions have no entity subject.
    pub fn entity_subject(&self) -> Option<EntityId> {
        use Proposition::*;
        match *self {
            LocatedIn { who, .. } => Some(who),
            Skimming { who, .. } => Some(who),
            BountyPosted { on, .. } => Some(on),
            OwesObligation { debtor, .. } => Some(debtor),
            Solvent { .. } | Insolvent { .. } | ControlsLane { .. } => None,
        }
    }
}

/// Why an entity holds a belief. Design §6.1.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Provenance {
    /// Requires the witness to have been in the region at the event tick.
    /// Asserted as an invariant.
    Witnessed,
    Told(EntityId),
    Inferred,
    /// A deliberate lie. Mechanically identical to the truth path — that is
    /// the setting's thesis expressed as a code path.
    Fabricated(EntityId),
}

/// Why a shipment failed to arrive on time. Typed rather than a bare flag so
/// the dump can explain itself and so new causes are a compile error to ignore.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DelayCause {
    LaneCongestion,
    /// Larger operators carry more overhead and miss more windows. This is the
    /// mean-reversion pressure of §6.5, routed through the existing causal
    /// machinery rather than bolted on as a separate rule.
    OperatorOverhead,
    CrewShortage,
}

/// Which archetype a generated contract belongs to. Design §6.6.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Archetype {
    Bounty,
    Audit,
    Collection,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Event {
    // ---- economy -------------------------------------------------------
    ShipmentDispatched {
        contract: ContractId,
    },
    ShipmentDelayed {
        contract: ContractId,
        cause: DelayCause,
    },
    ShipmentDelivered {
        contract: ContractId,
    },
    /// Money moves. It is never created or destroyed — see the conservation
    /// invariant in §6.5 and the property test that fuzzes it.
    CashTransferred {
        from: CorpId,
        to: CorpId,
        amount: Fx,
    },
    PenaltyTriggered {
        contract: ContractId,
        debtor: CorpId,
        creditor: CorpId,
        amount: Fx,
    },
    ReservesWentNegative {
        corp: CorpId,
    },
    /// Bankruptcy transfers assets to creditors; it never deletes them.
    LaneSold {
        lane: LaneId,
        from: CorpId,
        to: CorpId,
        price: Fx,
    },

    // ---- belief --------------------------------------------------------
    Witnessed {
        who: EntityId,
        proposition: Proposition,
    },
    Told {
        from: EntityId,
        to: EntityId,
        proposition: Proposition,
        confidence: Fx,
    },
    Fabricated {
        by: EntityId,
        to: EntityId,
        proposition: Proposition,
    },

    // ---- obligations ---------------------------------------------------
    ObligationIncurred {
        debtor: EntityId,
        creditor: EntityId,
        class: ObligationClass,
        weight: Fx,
        public: bool,
    },
    ObligationRedeemed {
        debtor: EntityId,
        creditor: EntityId,
        class: ObligationClass,
    },

    // ---- missions and investigation ------------------------------------
    ContractPosted {
        contract: ContractId,
        archetype: Archetype,
        poster: CorpId,
        target: EntityId,
    },
    /// The closing edge of the core loop (§4). Asking about someone is itself
    /// an event that other entities can learn about.
    InquiryMade {
        asker: EntityId,
        source: EntityId,
        about: EntityId,
    },
    TargetRelocated {
        who: EntityId,
        from: RegionId,
        to: RegionId,
    },
    ContractResolved {
        contract: ContractId,
        by: EntityId,
        alive: bool,
    },
}

/// An event as it sits in the log: identified, timestamped, and attributed to
/// the event that caused it.
#[derive(Clone, Copy, Debug)]
pub struct LoggedEvent {
    pub id: EventId,
    pub tick: Tick,
    pub region: RegionId,
    pub cause: Option<EventId>,
    pub event: Event,
}

/// Append-only. Design §5.3: snapshots are an optimisation, never a source of
/// truth, so this type deliberately offers no way to remove or rewrite an entry.
#[derive(Default)]
pub struct EventLog {
    entries: Vec<LoggedEvent>,
}

impl EventLog {
    pub fn new() -> EventLog {
        EventLog {
            entries: Vec::new(),
        }
    }

    pub fn append(
        &mut self,
        tick: Tick,
        region: RegionId,
        cause: Option<EventId>,
        event: Event,
    ) -> EventId {
        let id = EventId(self.entries.len() as u32);
        self.entries.push(LoggedEvent {
            id,
            tick,
            region,
            cause,
            event,
        });
        id
    }

    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    pub fn get(&self, id: EventId) -> Option<&LoggedEvent> {
        self.entries.get(id.0 as usize)
    }

    pub fn iter(&self) -> impl Iterator<Item = &LoggedEvent> {
        self.entries.iter()
    }

    /// Walks the causal chain backwards from an event, nearest cause first.
    /// This is the mechanism behind pillar P2's test.
    pub fn causal_chain(&self, from: EventId, max_depth: usize) -> Vec<&LoggedEvent> {
        let mut chain = Vec::new();
        let mut cursor = self.get(from).and_then(|e| e.cause);
        while let Some(id) = cursor {
            if chain.len() >= max_depth {
                break;
            }
            match self.get(id) {
                Some(e) => {
                    chain.push(e);
                    cursor = e.cause;
                }
                None => break,
            }
        }
        chain
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn location_claims_about_the_same_person_contradict() {
        let a = Proposition::LocatedIn {
            who: EntityId(1),
            region: RegionId(1),
        };
        let b = Proposition::LocatedIn {
            who: EntityId(1),
            region: RegionId(2),
        };
        let c = Proposition::LocatedIn {
            who: EntityId(2),
            region: RegionId(2),
        };
        assert!(a.contradicts(&b));
        assert!(!a.contradicts(&a));
        assert!(!a.contradicts(&c), "different people, no contradiction");
    }

    #[test]
    fn solvency_claims_contradict_both_ways() {
        let s = Proposition::Solvent { corp: CorpId(3) };
        let i = Proposition::Insolvent { corp: CorpId(3) };
        assert!(s.contradicts(&i));
        assert!(i.contradicts(&s));
        assert!(!s.contradicts(&Proposition::Insolvent { corp: CorpId(4) }));
    }

    #[test]
    fn causal_chain_walks_back_and_terminates() {
        let mut log = EventLog::new();
        let r = RegionId(0);
        let a = log.append(
            Tick(1),
            r,
            None,
            Event::ShipmentDelayed {
                contract: ContractId(0),
                cause: DelayCause::LaneCongestion,
            },
        );
        let b = log.append(
            Tick(2),
            r,
            Some(a),
            Event::PenaltyTriggered {
                contract: ContractId(0),
                debtor: CorpId(1),
                creditor: CorpId(2),
                amount: Fx::from_int(10),
            },
        );
        let c = log.append(Tick(3), r, Some(b), Event::ReservesWentNegative { corp: CorpId(1) });

        let chain = log.causal_chain(c, 10);
        assert_eq!(chain.len(), 2);
        assert_eq!(chain[0].id, b);
        assert_eq!(chain[1].id, a);
    }

    #[test]
    fn causal_chain_respects_depth_limit() {
        let mut log = EventLog::new();
        let mut prev = None;
        for t in 0..50 {
            prev = Some(log.append(
                Tick(t),
                RegionId(0),
                prev,
                Event::ReservesWentNegative { corp: CorpId(1) },
            ));
        }
        assert_eq!(log.causal_chain(prev.unwrap(), 4).len(), 4);
    }
}
