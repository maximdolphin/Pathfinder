//! Beliefs and their propagation. Design §6.1.
//!
//! Three concepts, never collapsed:
//!   - a **fact** is something true in the world (it lives in `World`)
//!   - a **belief** is an entity's held proposition about a fact, which may be false
//!   - **disposition** is computed from beliefs and *never stored* — see
//!     `BeliefStore::disposition_toward`. There is no `reputation: f32` in this
//!     codebase and there must never be one.
//!
//! Storage is a `Vec` indexed by entity, not a `HashMap`. Hash iteration order
//! is not stable, and any ordering that reaches authoritative state would break
//! bit-identical replay.

use crate::event::{Proposition, Provenance};
use crate::fixed::Fx;
use crate::ids::{EntityId, RegionId, Tick};
use crate::rng::Rng;

/// Confidence retained per hop of transmission.
///
/// Tuned against the §12 propagation-depth metric: at 0.72 a rumour was
/// forgotten before it reached a third listener and the median depth pinned at
/// one hop, which is a world where nothing travels. At 0.88 it crosses the
/// social graph and still degrades.
const HOP_RETENTION: Fx = Fx::permille(880);
/// Confidence lost per tick of age, absent corroboration.
const TICK_DECAY: Fx = Fx::permille(999);
/// Locations go stale much faster than facts about institutions. A person moves;
/// a corporation's solvency does not change under you while you walk across a
/// dock. Without this split the world accumulates confidently-held old addresses
/// and the false-belief share runs away.
const LOCATION_DECAY: Fx = Fx::permille(988);
/// Fear has a middle half-life. Knowing a contract is out on you outlasts a
/// rumour about where someone was standing, but it does not outlast the
/// contract — treat it as durable and the world fills with people still hiding
/// from a job that closed a year ago; treat it as news and no one stays
/// frightened long enough to run.
const BOUNTY_DECAY: Fx = Fx::permille(985);
/// A belief below this is forgotten rather than kept as noise.
const FORGET_THRESHOLD: Fx = Fx::permille(25);
/// One in this many transmissions mutates the proposition.
const DISTORTION_ODDS: u64 = 6;
/// Confidence retained in a held belief when a credible contradiction arrives
/// that is not strong enough to overturn it. Doubt, not conversion.
const DOUBT_RETENTION: Fx = Fx::permille(700);

#[derive(Clone, Copy, Debug)]
pub struct Belief {
    pub proposition: Proposition,
    pub confidence: Fx,
    pub acquired_tick: Tick,
    pub provenance: Provenance,
    /// Transmission distance from the original witness. Feeds the §12
    /// propagation-depth metric.
    pub hops: u8,
}

impl Belief {
    fn source(&self) -> Option<EntityId> {
        match self.provenance {
            Provenance::Told(e) | Provenance::Fabricated(e) => Some(e),
            Provenance::Witnessed | Provenance::Inferred => None,
        }
    }
}

#[derive(Default)]
pub struct BeliefStore {
    by_entity: Vec<Vec<Belief>>,
}

impl BeliefStore {
    pub fn with_capacity(entities: usize) -> BeliefStore {
        BeliefStore {
            by_entity: vec![Vec::new(); entities],
        }
    }

    pub fn held_by(&self, who: EntityId) -> &[Belief] {
        self.by_entity
            .get(who.0 as usize)
            .map(|v| v.as_slice())
            .unwrap_or(&[])
    }

    pub fn belief_about(&self, who: EntityId, predicate: impl Fn(&Proposition) -> bool) -> Option<&Belief> {
        self.held_by(who)
            .iter()
            .filter(|b| predicate(&b.proposition))
            .max_by_key(|b| b.confidence)
    }

    /// Insert or corroborate.
    ///
    /// Invariant (§6.1): a belief's confidence never increases without a *new
    /// corroborating event*. Being told the same thing again by the same source
    /// is not corroboration and changes nothing.
    pub fn record(&mut self, who: EntityId, incoming: Belief) {
        let slot = match self.by_entity.get_mut(who.0 as usize) {
            Some(s) => s,
            None => return,
        };

        if let Some(existing) = slot
            .iter_mut()
            .find(|b| b.proposition == incoming.proposition)
        {
            let novel_source = existing.source() != incoming.source();
            if novel_source {
                // Independent corroboration. Half the incoming confidence is
                // added, capped at certainty.
                existing.confidence =
                    (existing.confidence + incoming.confidence.scale_by(1, 2)).clamp01();
                existing.acquired_tick = incoming.acquired_tick;
                // Hops is *not* rewritten to the shorter path. It records how
                // far the news travelled to reach this holder the first time;
                // later corroboration confirms the claim, it does not change
                // how they came to hear it. Taking the minimum collapsed every
                // measurement to one or two hops in any well-connected graph.
            }
            return;
        }

        // A contradiction of something already held is not a second belief.
        // Nobody sincerely holds that a company is both solvent and insolvent;
        // they are convinced, or they doubt. Accumulating both filled the world
        // with incoherent holders and made the contradiction metric measure
        // storage rather than disagreement.
        if let Some(position) = slot
            .iter()
            .position(|b| b.proposition.contradicts(&incoming.proposition))
        {
            if incoming.confidence > slot[position].confidence {
                // Convinced: the stronger claim replaces the weaker one.
                slot[position] = incoming;
            } else {
                // Doubt: the held belief survives, weakened. Confidence only
                // ever falls here, so the §6.1 monotonicity invariant holds.
                slot[position].confidence = slot[position].confidence.mul(DOUBT_RETENTION);
            }
            return;
        }

        slot.push(incoming);
    }

    /// Beliefs this entity holds that contradict the given proposition.
    pub fn contradicting(&self, who: EntityId, proposition: &Proposition) -> Vec<Belief> {
        self.held_by(who)
            .iter()
            .filter(|b| b.proposition.contradicts(proposition))
            .copied()
            .collect()
    }

    /// Disposition is **computed, never stored** (§6.1). An entity's stance
    /// toward a subject is a pure function of the beliefs it holds about that
    /// subject — which is what gives locality, witness elimination, and two
    /// factions holding opposite views of the same act, all for free.
    pub fn disposition_toward(&self, holder: EntityId, subject: EntityId) -> Fx {
        let mut score = Fx::ZERO;
        for belief in self.held_by(holder) {
            if belief.proposition.entity_subject() != Some(subject) {
                continue;
            }
            let weight = match belief.proposition {
                Proposition::Skimming { .. } => -Fx::permille(600),
                Proposition::BountyPosted { .. } => -Fx::permille(400),
                Proposition::OwesObligation { .. } => -Fx::permille(200),
                Proposition::LocatedIn { .. } => Fx::ZERO,
                Proposition::Solvent { .. }
                | Proposition::Insolvent { .. }
                | Proposition::ControlsLane { .. } => Fx::ZERO,
            };
            score += weight.mul(belief.confidence);
        }
        score
    }

    /// Age every belief by one tick and forget what has decayed to noise.
    /// Monotonically non-increasing — this is the other half of the §6.1
    /// confidence invariant.
    pub fn decay_all(&mut self) {
        for slot in self.by_entity.iter_mut() {
            for belief in slot.iter_mut() {
                belief.confidence = belief.confidence.mul(decay_rate(&belief.proposition));
            }
            slot.retain(|b| b.confidence > FORGET_THRESHOLD);
        }
    }

    pub fn total_beliefs(&self) -> usize {
        self.by_entity.iter().map(|s| s.len()).sum()
    }

    pub fn all(&self) -> impl Iterator<Item = (EntityId, &Belief)> {
        self.by_entity
            .iter()
            .enumerate()
            .flat_map(|(i, slot)| slot.iter().map(move |b| (EntityId(i as u32), b)))
    }
}

/// How fast a proposition goes stale. Exhaustive, so a new proposition cannot
/// be added without deciding how perishable it is (§8.4).
fn decay_rate(proposition: &Proposition) -> Fx {
    match proposition {
        // Perishable: these are events, and an event is only news until it is
        // superseded. Someone moves, a lane changes hands again, a contract
        // closes — and the claim is quietly wrong while still being repeated.
        Proposition::LocatedIn { .. } | Proposition::ControlsLane { .. } => LOCATION_DECAY,
        // Fast, now that a source warns a friend directly (see `step_agent`).
        // The target learns in one hop instead of waiting for the rumour mill,
        // so fear no longer has to be long-lived to reach them — and stale
        // bounty talk stops accumulating into most of the world's beliefs.
        Proposition::BountyPosted { .. } => BOUNTY_DECAY,
        // Durable: standing facts about institutions and people.
        Proposition::Solvent { .. }
        | Proposition::Insolvent { .. }
        | Proposition::Skimming { .. }
        | Proposition::OwesObligation { .. } => TICK_DECAY,
    }
}

/// Confidence a belief arrives with after one hop of transmission.
pub fn transmitted_confidence(source: Fx) -> Fx {
    source.mul(HOP_RETENTION)
}

/// Deterministic distortion, seeded by `(source, target, tick)` per §6.1.
///
/// Note there is no special case per proposition variant beyond the shape of
/// the data itself — the match is exhaustive, so adding a proposition is a
/// compile error until its distortion is defined (§8.4).
pub fn distort(
    proposition: Proposition,
    rng: &mut Rng,
    region_count: u32,
) -> Proposition {
    if !rng.chance(1, DISTORTION_ODDS) {
        return proposition;
    }
    match proposition {
        Proposition::LocatedIn { who, region } => {
            // Rumour drifts to a neighbouring region rather than anywhere.
            let shift = if rng.chance(1, 2) { 1 } else { region_count - 1 };
            Proposition::LocatedIn {
                who,
                region: RegionId((region.0 + shift) % region_count),
            }
        }
        Proposition::Solvent { corp } => Proposition::Insolvent { corp },
        Proposition::Insolvent { corp } => Proposition::Solvent { corp },
        // Distorting these would fabricate an identity rather than blur a
        // detail, which is what `Provenance::Fabricated` is for. Left intact.
        Proposition::ControlsLane { .. }
        | Proposition::Skimming { .. }
        | Proposition::BountyPosted { .. }
        | Proposition::OwesObligation { .. } => proposition,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ids::{CorpId, RegionId};

    fn belief(p: Proposition, c: Fx, from: Option<EntityId>) -> Belief {
        Belief {
            proposition: p,
            confidence: c,
            acquired_tick: Tick(0),
            provenance: match from {
                Some(e) => Provenance::Told(e),
                None => Provenance::Witnessed,
            },
            hops: 0,
        }
    }

    fn located(region: u32) -> Proposition {
        Proposition::LocatedIn {
            who: EntityId(9),
            region: RegionId(region),
        }
    }

    #[test]
    fn repeat_from_same_source_is_not_corroboration() {
        let mut store = BeliefStore::with_capacity(4);
        let src = Some(EntityId(1));
        store.record(EntityId(0), belief(located(1), Fx::permille(400), src));
        store.record(EntityId(0), belief(located(1), Fx::permille(400), src));
        assert_eq!(store.held_by(EntityId(0)).len(), 1);
        assert_eq!(store.held_by(EntityId(0))[0].confidence, Fx::permille(400));
    }

    #[test]
    fn independent_source_corroborates() {
        let mut store = BeliefStore::with_capacity(4);
        store.record(EntityId(0), belief(located(1), Fx::permille(400), Some(EntityId(1))));
        store.record(EntityId(0), belief(located(1), Fx::permille(400), Some(EntityId(2))));
        assert_eq!(store.held_by(EntityId(0))[0].confidence, Fx::permille(600));
    }

    #[test]
    fn corroboration_cannot_exceed_certainty() {
        let mut store = BeliefStore::with_capacity(4);
        store.record(EntityId(0), belief(located(1), Fx::permille(900), Some(EntityId(1))));
        store.record(EntityId(0), belief(located(1), Fx::ONE, Some(EntityId(2))));
        assert_eq!(store.held_by(EntityId(0))[0].confidence, Fx::ONE);
    }

    #[test]
    fn decay_is_monotonically_non_increasing() {
        let mut store = BeliefStore::with_capacity(2);
        store.record(EntityId(0), belief(located(1), Fx::permille(900), None));
        let mut previous = store.held_by(EntityId(0))[0].confidence;
        for _ in 0..200 {
            store.decay_all();
            match store.held_by(EntityId(0)).first() {
                Some(b) => {
                    assert!(b.confidence <= previous, "confidence rose during decay");
                    previous = b.confidence;
                }
                None => return, // forgotten, which is allowed
            }
        }
    }

    #[test]
    fn contradicting_finds_the_rival_claim() {
        let mut store = BeliefStore::with_capacity(2);
        store.record(EntityId(0), belief(located(1), Fx::permille(500), None));
        assert_eq!(store.contradicting(EntityId(0), &located(2)).len(), 1);
        assert_eq!(store.contradicting(EntityId(0), &located(1)).len(), 0);
    }

    #[test]
    fn disposition_is_derived_not_stored() {
        let mut store = BeliefStore::with_capacity(2);
        let neutral = store.disposition_toward(EntityId(0), EntityId(9));
        assert_eq!(neutral, Fx::ZERO);

        store.record(
            EntityId(0),
            belief(
                Proposition::Skimming {
                    who: EntityId(9),
                    from: CorpId(1),
                },
                Fx::permille(500),
                None,
            ),
        );
        assert!(store.disposition_toward(EntityId(0), EntityId(9)).is_negative());
        // …and a different holder, who was told nothing, still feels nothing.
        assert_eq!(store.disposition_toward(EntityId(1), EntityId(9)), Fx::ZERO);
    }

    #[test]
    fn distortion_is_deterministic_for_the_same_seed() {
        let p = located(1);
        let a = distort(p, &mut Rng::seeded(7, RegionId(0), Tick(3)), 5);
        let b = distort(p, &mut Rng::seeded(7, RegionId(0), Tick(3)), 5);
        assert_eq!(a, b);
    }

    #[test]
    fn distorted_location_stays_in_range() {
        for tick in 0..500 {
            let mut rng = Rng::seeded(1, RegionId(0), Tick(tick));
            if let Proposition::LocatedIn { region, .. } = distort(located(2), &mut rng, 5) {
                assert!(region.0 < 5);
            }
        }
    }
}
