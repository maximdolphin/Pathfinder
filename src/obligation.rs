//! The obligation ledger. Design §6.2.
//!
//! A directed multigraph of favours owed, **not a stat**. There is no
//! `relationship: f32`. Favours are consumed on redemption; there is no
//! renewable relationship currency.

use crate::fixed::Fx;
use crate::ids::{EntityId, Tick};

/// What a favour can actually buy. Design §6.2: the class gates the payload —
/// a dockworker yields a manifest, a port authority yields a clean landing, an
/// executive yields a name unredacted. Typed, so an unhandled class is a
/// compile error (§8.2, §8.4).
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Debug)]
pub enum ObligationClass {
    /// Cargo manifests, crew lists, schedules.
    Manifest,
    /// Landing without an inspection.
    CleanLanding,
    /// A name that the filing had redacted.
    UnredactedName,
    /// Not reporting what was seen.
    Silence,
    /// Plain money owed.
    Debt,
}

impl ObligationClass {
    pub const ALL: [ObligationClass; 5] = [
        ObligationClass::Manifest,
        ObligationClass::CleanLanding,
        ObligationClass::UnredactedName,
        ObligationClass::Silence,
        ObligationClass::Debt,
    ];

    /// Whether redeeming this class discloses a location. Used by the
    /// investigation algorithm to decide what a source can actually give up.
    pub fn yields_location(self) -> bool {
        match self {
            ObligationClass::Manifest | ObligationClass::UnredactedName => true,
            ObligationClass::CleanLanding | ObligationClass::Silence | ObligationClass::Debt => {
                false
            }
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            ObligationClass::Manifest => "a manifest",
            ObligationClass::CleanLanding => "a clean landing",
            ObligationClass::UnredactedName => "an unredacted name",
            ObligationClass::Silence => "silence",
            ObligationClass::Debt => "a debt",
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub struct Obligation {
    pub debtor: EntityId,
    pub creditor: EntityId,
    pub class: ObligationClass,
    pub weight: Fx,
    pub incurred: Tick,
    pub expiry: Option<Tick>,
    /// Was it witnessed? Public obligations are visible to anyone holding the
    /// belief; private ones are leverage.
    pub public: bool,
}

#[derive(Default)]
pub struct Ledger {
    open: Vec<Obligation>,
    redeemed: usize,
    incurred: usize,
}

impl Ledger {
    pub fn new() -> Ledger {
        Ledger::default()
    }

    pub fn incur(&mut self, obligation: Obligation) {
        self.incurred += 1;
        self.open.push(obligation);
    }

    /// Favours are **consumed** on redemption (§6.2). Returns the obligation
    /// that was spent, so the caller can emit the event describing it.
    pub fn redeem(
        &mut self,
        debtor: EntityId,
        creditor: EntityId,
        class: ObligationClass,
    ) -> Option<Obligation> {
        let index = self
            .open
            .iter()
            .position(|o| o.debtor == debtor && o.creditor == creditor && o.class == class)?;
        self.redeemed += 1;
        Some(self.open.swap_remove(index))
    }

    /// Everything `creditor` can call in, strongest first. Deterministic order:
    /// ties break on debtor id, never on insertion accident.
    pub fn held_by(&self, creditor: EntityId) -> Vec<Obligation> {
        let mut held: Vec<Obligation> = self
            .open
            .iter()
            .filter(|o| o.creditor == creditor)
            .copied()
            .collect();
        held.sort_by_key(|o| (std::cmp::Reverse(o.weight), o.debtor, o.class));
        held
    }

    pub fn owed_by(&self, debtor: EntityId) -> Vec<Obligation> {
        let mut owed: Vec<Obligation> = self
            .open
            .iter()
            .filter(|o| o.debtor == debtor)
            .copied()
            .collect();
        owed.sort_by_key(|o| (std::cmp::Reverse(o.weight), o.creditor, o.class));
        owed
    }

    /// Drops obligations past their expiry. Returns how many lapsed.
    pub fn expire(&mut self, now: Tick) -> usize {
        let before = self.open.len();
        self.open.retain(|o| match o.expiry {
            Some(t) => now <= t,
            None => true,
        });
        before - self.open.len()
    }

    pub fn open_count(&self) -> usize {
        self.open.len()
    }

    pub fn iter(&self) -> impl Iterator<Item = &Obligation> {
        self.open.iter()
    }

    /// §12 metric: too low and favours feel worthless, too high and they are
    /// trivially farmed. Target band is 40–70%.
    pub fn redemption_rate(&self) -> Fx {
        if self.incurred == 0 {
            return Fx::ZERO;
        }
        Fx::from_int(self.redeemed as i64).div(Fx::from_int(self.incurred as i64))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn obligation(debtor: u32, creditor: u32, class: ObligationClass) -> Obligation {
        Obligation {
            debtor: EntityId(debtor),
            creditor: EntityId(creditor),
            class,
            weight: Fx::permille(500),
            incurred: Tick(0),
            expiry: None,
            public: false,
        }
    }

    #[test]
    fn redemption_consumes_the_favour() {
        let mut ledger = Ledger::new();
        ledger.incur(obligation(1, 2, ObligationClass::Manifest));
        assert!(ledger
            .redeem(EntityId(1), EntityId(2), ObligationClass::Manifest)
            .is_some());
        assert_eq!(ledger.open_count(), 0);
        assert!(
            ledger
                .redeem(EntityId(1), EntityId(2), ObligationClass::Manifest)
                .is_none(),
            "a spent favour must not be spendable twice"
        );
    }

    #[test]
    fn redeeming_one_leaves_the_others_intact() {
        let mut ledger = Ledger::new();
        ledger.incur(obligation(1, 2, ObligationClass::Manifest));
        ledger.incur(obligation(1, 2, ObligationClass::Silence));
        ledger.incur(obligation(3, 2, ObligationClass::Manifest));
        ledger.redeem(EntityId(1), EntityId(2), ObligationClass::Manifest);
        assert_eq!(ledger.open_count(), 2);
        assert_eq!(ledger.held_by(EntityId(2)).len(), 2);
    }

    #[test]
    fn held_by_ordering_is_deterministic() {
        let mut a = Ledger::new();
        let mut b = Ledger::new();
        a.incur(obligation(3, 9, ObligationClass::Silence));
        a.incur(obligation(1, 9, ObligationClass::Manifest));
        b.incur(obligation(1, 9, ObligationClass::Manifest));
        b.incur(obligation(3, 9, ObligationClass::Silence));
        let ids_a: Vec<EntityId> = a.held_by(EntityId(9)).iter().map(|o| o.debtor).collect();
        let ids_b: Vec<EntityId> = b.held_by(EntityId(9)).iter().map(|o| o.debtor).collect();
        assert_eq!(ids_a, ids_b, "insertion order must not affect ordering");
    }

    #[test]
    fn expiry_drops_only_lapsed_favours() {
        let mut ledger = Ledger::new();
        let mut short = obligation(1, 2, ObligationClass::Debt);
        short.expiry = Some(Tick(10));
        ledger.incur(short);
        ledger.incur(obligation(3, 2, ObligationClass::Debt));
        assert_eq!(ledger.expire(Tick(5)), 0);
        assert_eq!(ledger.expire(Tick(11)), 1);
        assert_eq!(ledger.open_count(), 1);
    }

    #[test]
    fn redemption_rate_tracks_both_halves() {
        let mut ledger = Ledger::new();
        assert_eq!(ledger.redemption_rate(), Fx::ZERO);
        for i in 0..4 {
            ledger.incur(obligation(i, 9, ObligationClass::Debt));
        }
        ledger.redeem(EntityId(0), EntityId(9), ObligationClass::Debt);
        ledger.redeem(EntityId(1), EntityId(9), ObligationClass::Debt);
        assert_eq!(ledger.redemption_rate(), Fx::permille(500));
    }
}
