//! The hidden-variable extraction algorithm. Design §6.3.
//!
//! One algorithm, reused by every investigation-class mission. "Find a person"
//! is the same problem as "find who is skimming from a manifest" and "find the
//! debtor's whereabouts" — in each case the player holds a search volume `V`
//! over possible values of a hidden variable and each disclosure constrains it.
//!
//! Two properties are load-bearing and are tested here:
//!   - contradictory disclosures **widen** `V`; they never silently pick a winner
//!   - there are no quest markers. `V` is a region set, not a pin.

use crate::fixed::Fx;
use crate::ids::RegionId;
use crate::obligation::ObligationClass;
use std::collections::BTreeSet;

/// What a disclosure did to the search volume. Returned so the caller can
/// narrate it — a contradiction is information, and the player should see it.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Constraint {
    Narrowed,
    Widened,
    NoChange,
}

/// `BTreeSet` rather than `HashSet`: iteration order reaches the text dump and
/// therefore must be stable.
#[derive(Clone, Debug)]
pub struct SearchVolume {
    regions: BTreeSet<RegionId>,
    domain: u32,
}

impl SearchVolume {
    pub fn full(domain: u32) -> SearchVolume {
        SearchVolume {
            regions: (0..domain).map(RegionId).collect(),
            domain,
        }
    }

    /// Intersect with a claimed region. If the claim is incompatible with
    /// everything we currently believe, the volume widens to admit it — which
    /// is exactly what a sincere contradiction should do to your confidence.
    pub fn constrain(&mut self, claim: RegionId) -> Constraint {
        if self.regions.contains(&claim) {
            if self.regions.len() == 1 {
                return Constraint::NoChange;
            }
            self.regions.clear();
            self.regions.insert(claim);
            Constraint::Narrowed
        } else {
            self.regions.insert(claim);
            Constraint::Widened
        }
    }

    /// `|V|`.
    pub fn size(&self) -> usize {
        self.regions.len()
    }

    pub fn contains(&self, region: RegionId) -> bool {
        self.regions.contains(&region)
    }

    pub fn regions(&self) -> impl Iterator<Item = RegionId> + '_ {
        self.regions.iter().copied()
    }

    /// Resolved when the volume is down to a single region.
    pub fn resolved(&self) -> Option<RegionId> {
        if self.regions.len() == 1 {
            self.regions.iter().next().copied()
        } else {
            None
        }
    }

    /// Fraction of the domain still in play. 1.0 is "no idea", small is close.
    pub fn resolution(&self) -> Fx {
        Fx::from_int(self.regions.len() as i64).div(Fx::from_int(self.domain as i64))
    }
}

/// What the asker can offer for a disclosure. Design §6.3: incentives are
/// money, favour redemption, threat, and traded information.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Incentive {
    Money(u32),
    RedeemFavour(ObligationClass),
    Threat,
}

impl Incentive {
    /// How much this shifts a reluctant source. Threat works but it is loud —
    /// the caller is expected to emit a witnessable event for it.
    pub fn persuasion(self) -> Fx {
        match self {
            Incentive::Money(amount) => {
                Fx::permille((amount as i64).min(600))
            }
            Incentive::RedeemFavour(class) => {
                if class.yields_location() {
                    Fx::permille(800)
                } else {
                    Fx::permille(200)
                }
            }
            Incentive::Threat => Fx::permille(700),
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Incentive::Money(_) => "money",
            Incentive::RedeemFavour(_) => "a favour called in",
            Incentive::Threat => "a threat",
        }
    }
}

/// Design §6.3: disclosure is a function of `(disposition, perceived_risk,
/// incentive_offered, relationship_to_target)`.
///
/// There is no special case per source type here. The inputs are the whole
/// story, which is what §8.4 asks for — you can state the rule without
/// enumerating exceptions.
pub fn will_disclose(
    disposition_toward_asker: Fx,
    disposition_toward_target: Fx,
    perceived_risk: Fx,
    incentive: Incentive,
) -> bool {
    // Liking the asker helps; liking the target hurts; risk hurts.
    let willingness = disposition_toward_asker + incentive.persuasion() - perceived_risk
        - disposition_toward_target;
    willingness > Fx::permille(250)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_volume_covers_the_domain() {
        let v = SearchVolume::full(5);
        assert_eq!(v.size(), 5);
        assert_eq!(v.resolution(), Fx::ONE);
        assert!(v.resolved().is_none());
    }

    #[test]
    fn a_consistent_claim_narrows_to_one_region() {
        let mut v = SearchVolume::full(5);
        assert_eq!(v.constrain(RegionId(3)), Constraint::Narrowed);
        assert_eq!(v.size(), 1);
        assert_eq!(v.resolved(), Some(RegionId(3)));
    }

    #[test]
    fn a_contradictory_claim_widens_rather_than_picking_a_winner() {
        let mut v = SearchVolume::full(5);
        v.constrain(RegionId(3));
        assert_eq!(v.constrain(RegionId(1)), Constraint::Widened);
        assert_eq!(v.size(), 2, "both claims must remain in play");
        assert!(v.contains(RegionId(3)) && v.contains(RegionId(1)));
        assert!(v.resolved().is_none());
    }

    #[test]
    fn repeating_a_resolved_claim_changes_nothing() {
        let mut v = SearchVolume::full(5);
        v.constrain(RegionId(2));
        assert_eq!(v.constrain(RegionId(2)), Constraint::NoChange);
        assert_eq!(v.size(), 1);
    }

    #[test]
    fn contradiction_then_corroboration_reconverges() {
        let mut v = SearchVolume::full(6);
        v.constrain(RegionId(0));
        v.constrain(RegionId(4)); // widened to {0,4}
        assert_eq!(v.size(), 2);
        assert_eq!(v.constrain(RegionId(4)), Constraint::Narrowed);
        assert_eq!(v.resolved(), Some(RegionId(4)));
    }

    #[test]
    fn iteration_order_is_stable() {
        let mut a = SearchVolume::full(4);
        let mut b = SearchVolume::full(4);
        a.constrain(RegionId(3));
        a.constrain(RegionId(1));
        b.constrain(RegionId(1));
        b.constrain(RegionId(3));
        let ra: Vec<RegionId> = a.regions().collect();
        let rb: Vec<RegionId> = b.regions().collect();
        assert_eq!(ra, rb);
    }

    #[test]
    fn a_favour_that_yields_a_location_outweighs_small_money() {
        assert!(
            Incentive::RedeemFavour(ObligationClass::Manifest).persuasion()
                > Incentive::Money(100).persuasion()
        );
    }

    #[test]
    fn loyalty_to_the_target_suppresses_disclosure() {
        let loyal = will_disclose(
            Fx::permille(300),
            Fx::permille(900),
            Fx::permille(100),
            Incentive::Money(200),
        );
        assert!(!loyal, "a source loyal to the target should stay quiet");

        let indifferent = will_disclose(
            Fx::permille(300),
            Fx::ZERO,
            Fx::permille(100),
            Incentive::Money(200),
        );
        assert!(indifferent);
    }

    #[test]
    fn risk_can_outweigh_any_incentive() {
        assert!(!will_disclose(
            Fx::ZERO,
            Fx::ZERO,
            Fx::from_int(2),
            Incentive::Threat
        ));
    }
}
