//! Mission generation. Design §6.6.
//!
//! Missions are **queries against existing simulation tension**, not templates
//! with blanks filled from a pool. The distinction is the whole design:
//!
//! ```text
//! find (A, B) where
//!     A holds unresolved obligation against B
//!     and B has an exploitable vulnerability
//!     and the tension predates the player's awareness of it
//! ```
//!
//! The goal is generated from state that already existed for its own reasons,
//! which is why it reads as authored. §12 tracks the fraction of contracts that
//! reference pre-existing tension; if that drops, this module has regressed
//! into a template generator and the oatmeal problem (§14 R2) has arrived.

use crate::event::Archetype;
use crate::ids::{ContractId, CorpId, EntityId, Tick};
use crate::obligation::{Ledger, ObligationClass};

/// A tension the world produced on its own, and the raw material of a contract.
#[derive(Clone, Copy, Debug)]
pub struct Tension {
    pub creditor_corp: CorpId,
    pub creditor: EntityId,
    pub debtor: EntityId,
    pub class: ObligationClass,
    /// When the obligation was incurred. A contract is only legitimate if this
    /// predates the posting — that is what "consequent, not generated" means.
    pub incurred: Tick,
}

impl Tension {
    /// The archetype follows from what the obligation is, not from a roll.
    /// Exhaustive, so a new obligation class cannot be added without deciding
    /// what kind of mission it produces (§8.4).
    pub fn archetype(&self) -> Archetype {
        match self.class {
            // Someone bought silence. Whatever they are hiding is worth a body.
            ObligationClass::Silence => Archetype::Bounty,
            ObligationClass::Debt => Archetype::Collection,
            ObligationClass::Manifest
            | ObligationClass::CleanLanding
            | ObligationClass::UnredactedName => Archetype::Audit,
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct MissionContract {
    pub id: ContractId,
    pub archetype: Archetype,
    pub poster: CorpId,
    pub target: EntityId,
    pub posted: Tick,
    pub tension_incurred: Tick,
    pub resolved: bool,
}

impl MissionContract {
    /// §12: "contracts referencing pre-existing tension > 90%". A contract
    /// whose tension was incurred at the moment of posting is a template in
    /// disguise, and this is how we detect that.
    pub fn references_pre_existing_tension(&self) -> bool {
        self.tension_incurred < self.posted
    }
}

/// Run the query. `employer_of` resolves an entity to the corp it works for,
/// and `vulnerability` scores how exposed a debtor is — a pure read of belief
/// state supplied by the caller so this module depends on a narrow function
/// rather than on `BeliefStore` itself (§8.3, interface segregation).
pub fn find_tension(
    ledger: &Ledger,
    now: Tick,
    employer_of: impl Fn(EntityId) -> Option<CorpId>,
    vulnerability: impl Fn(EntityId) -> bool,
) -> Option<Tension> {
    let mut candidates: Vec<Tension> = ledger
        .iter()
        .filter(|o| o.incurred < now)
        .filter(|o| vulnerability(o.debtor))
        .filter_map(|o| {
            employer_of(o.creditor).map(|corp| Tension {
                creditor_corp: corp,
                creditor: o.creditor,
                debtor: o.debtor,
                class: o.class,
                incurred: o.incurred,
            })
        })
        .collect();

    // Oldest tension first: a grudge that has been sitting for a thousand ticks
    // is a better story than one from last week, and fixing the order keeps
    // generation deterministic.
    candidates.sort_by_key(|t| (t.incurred, t.debtor, t.creditor, t.class));
    candidates.into_iter().next()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fixed::Fx;
    use crate::obligation::Obligation;

    fn ledger_with(entries: &[(u32, u32, ObligationClass, u64)]) -> Ledger {
        let mut ledger = Ledger::new();
        for (debtor, creditor, class, incurred) in entries {
            ledger.incur(Obligation {
                debtor: EntityId(*debtor),
                creditor: EntityId(*creditor),
                class: *class,
                weight: Fx::permille(500),
                incurred: Tick(*incurred),
                expiry: None,
                public: false,
            });
        }
        ledger
    }

    #[test]
    fn archetype_follows_from_the_obligation_not_a_roll() {
        let base = Tension {
            creditor_corp: CorpId(0),
            creditor: EntityId(1),
            debtor: EntityId(2),
            class: ObligationClass::Silence,
            incurred: Tick(0),
        };
        assert_eq!(base.archetype(), Archetype::Bounty);
        assert_eq!(
            Tension {
                class: ObligationClass::Debt,
                ..base
            }
            .archetype(),
            Archetype::Collection
        );
        assert_eq!(
            Tension {
                class: ObligationClass::Manifest,
                ..base
            }
            .archetype(),
            Archetype::Audit
        );
    }

    #[test]
    fn tension_must_predate_the_query() {
        let ledger = ledger_with(&[(2, 1, ObligationClass::Debt, 100)]);
        assert!(find_tension(&ledger, Tick(50), |_| Some(CorpId(0)), |_| true).is_none());
        assert!(find_tension(&ledger, Tick(150), |_| Some(CorpId(0)), |_| true).is_some());
    }

    #[test]
    fn an_invulnerable_debtor_produces_no_contract() {
        let ledger = ledger_with(&[(2, 1, ObligationClass::Debt, 10)]);
        assert!(find_tension(&ledger, Tick(50), |_| Some(CorpId(0)), |_| false).is_none());
    }

    #[test]
    fn a_creditor_with_no_employer_produces_no_contract() {
        let ledger = ledger_with(&[(2, 1, ObligationClass::Debt, 10)]);
        assert!(find_tension(&ledger, Tick(50), |_| None, |_| true).is_none());
    }

    #[test]
    fn the_oldest_grudge_wins_and_selection_is_deterministic() {
        let ledger = ledger_with(&[
            (5, 1, ObligationClass::Debt, 900),
            (6, 1, ObligationClass::Silence, 30),
            (7, 1, ObligationClass::Manifest, 400),
        ]);
        let found = find_tension(&ledger, Tick(1000), |_| Some(CorpId(0)), |_| true).unwrap();
        assert_eq!(found.debtor, EntityId(6));
        assert_eq!(found.archetype(), Archetype::Bounty);
    }

    #[test]
    fn contract_detects_tension_manufactured_at_posting_time() {
        let honest = MissionContract {
            id: ContractId(0),
            archetype: Archetype::Bounty,
            poster: CorpId(0),
            target: EntityId(1),
            posted: Tick(500),
            tension_incurred: Tick(120),
            resolved: false,
        };
        assert!(honest.references_pre_existing_tension());

        let fake = MissionContract {
            tension_incurred: Tick(500),
            ..honest
        };
        assert!(
            !fake.references_pre_existing_tension(),
            "tension created at posting time is a template, not a consequence"
        );
    }
}
