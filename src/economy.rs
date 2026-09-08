//! Corporations, lanes, shipments. Design §6.5.
//!
//! Causality direction is fixed: `simulation event → supply/demand → price →
//! news`. Never the inverse. Nothing in this module reads a headline.
//!
//! **Conservation.** Quantities move, they are never destroyed. Cash is only
//! ever transferred between corps; a lane always has exactly one owner. This is
//! what stops a persistent world from monotonically degrading into a husk, and
//! it is fuzzed as an invariant in `tests/invariants.rs`.

use crate::fixed::Fx;
use crate::ids::{ContractId, CorpId, LaneId, RegionId, Tick};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ShipmentState {
    InTransit,
    Delayed,
    Delivered,
    /// Late past the point where the penalty fired.
    Defaulted,
}

#[derive(Clone, Copy, Debug)]
pub struct Shipment {
    pub id: ContractId,
    pub shipper: CorpId,
    pub client: CorpId,
    pub lane: LaneId,
    pub dispatched: Tick,
    pub due: Tick,
    pub penalty: Fx,
    pub state: ShipmentState,
}

#[derive(Clone, Debug)]
pub struct Corp {
    pub id: CorpId,
    pub name: &'static str,
    pub cash: Fx,
    pub home: RegionId,
}

pub struct Economy {
    corps: Vec<Corp>,
    /// Indexed by `LaneId`. Every lane always has exactly one owner, which is
    /// how the "territory is conserved" invariant is made structural rather
    /// than checked.
    lane_owner: Vec<CorpId>,
    shipments: Vec<Shipment>,
}

impl Economy {
    pub fn new(corps: Vec<Corp>, lane_owner: Vec<CorpId>) -> Economy {
        Economy {
            corps,
            lane_owner,
            shipments: Vec::new(),
        }
    }

    // ---- reads ---------------------------------------------------------

    pub fn corps(&self) -> &[Corp] {
        &self.corps
    }

    pub fn corp(&self, id: CorpId) -> &Corp {
        &self.corps[id.0 as usize]
    }

    pub fn lane_count(&self) -> usize {
        self.lane_owner.len()
    }

    pub fn lane_owner(&self, lane: LaneId) -> CorpId {
        self.lane_owner[lane.0 as usize]
    }

    pub fn lanes_of(&self, corp: CorpId) -> Vec<LaneId> {
        self.lane_owner
            .iter()
            .enumerate()
            .filter(|(_, owner)| **owner == corp)
            .map(|(i, _)| LaneId(i as u32))
            .collect()
    }

    pub fn lanes_held(&self, corp: CorpId) -> usize {
        self.lane_owner.iter().filter(|o| **o == corp).count()
    }

    pub fn shipments(&self) -> &[Shipment] {
        &self.shipments
    }

    pub fn shipment(&self, id: ContractId) -> &Shipment {
        &self.shipments[id.0 as usize]
    }

    pub fn total_cash(&self) -> Fx {
        self.corps.iter().fold(Fx::ZERO, |acc, c| acc + c.cash)
    }

    /// Largest single share of lanes, as a fraction. §6.5 forbids any faction
    /// exceeding 40% for a sustained run; §12 tracks it.
    pub fn dominance(&self) -> Fx {
        let total = self.lane_count();
        if total == 0 {
            return Fx::ZERO;
        }
        let most = self
            .corps
            .iter()
            .map(|c| self.lanes_held(c.id))
            .max()
            .unwrap_or(0);
        Fx::from_int(most as i64).div(Fx::from_int(total as i64))
    }

    /// Gini over lane holdings. §12 wants this bounded and oscillating; a
    /// rising trend means ossification.
    pub fn lane_gini(&self) -> Fx {
        let holdings: Vec<i64> = self
            .corps
            .iter()
            .map(|c| self.lanes_held(c.id) as i64)
            .collect();
        let n = holdings.len() as i64;
        let total: i64 = holdings.iter().sum();
        if n == 0 || total == 0 {
            return Fx::ZERO;
        }
        let mut absolute_differences: i64 = 0;
        for a in &holdings {
            for b in &holdings {
                absolute_differences += (a - b).abs();
            }
        }
        // G = sum|xi - xj| / (2 * n * sum(x))
        Fx::from_int(absolute_differences).div(Fx::from_int(2 * n * total))
    }

    /// Larger operators miss more delivery windows. This is the mean-reversion
    /// pressure of §6.5 expressed through the existing causal machinery rather
    /// than as a separate corrective rule — big corps are not punished, they
    /// simply default more, and the defaults do the work.
    pub fn overhead_delay_odds(&self, corp: CorpId) -> u64 {
        let held = self.lanes_held(corp) as u64;
        // One in twelve at no lanes, rising toward one in four when dominant.
        12u64.saturating_sub(held).max(4)
    }

    // ---- writes (only ever called from World::apply) --------------------

    pub fn transfer(&mut self, from: CorpId, to: CorpId, amount: Fx) {
        if from == to {
            return;
        }
        self.corps[from.0 as usize].cash -= amount;
        self.corps[to.0 as usize].cash += amount;
    }

    pub fn set_lane_owner(&mut self, lane: LaneId, to: CorpId) {
        self.lane_owner[lane.0 as usize] = to;
    }

    pub fn add_shipment(&mut self, shipment: Shipment) -> ContractId {
        let id = ContractId(self.shipments.len() as u32);
        self.shipments.push(Shipment { id, ..shipment });
        id
    }

    pub fn set_shipment_state(&mut self, id: ContractId, state: ShipmentState) {
        self.shipments[id.0 as usize].state = state;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn economy() -> Economy {
        let corps = (0..4)
            .map(|i| Corp {
                id: CorpId(i),
                name: "test",
                cash: Fx::from_int(1000),
                home: RegionId(0),
            })
            .collect();
        // 12 lanes, 3 each.
        let lane_owner = (0..12).map(|i| CorpId(i % 4)).collect();
        Economy::new(corps, lane_owner)
    }

    #[test]
    fn transfer_conserves_total_cash() {
        let mut e = economy();
        let before = e.total_cash();
        e.transfer(CorpId(0), CorpId(1), Fx::from_int(250));
        e.transfer(CorpId(1), CorpId(3), Fx::from_int(90));
        assert_eq!(e.total_cash(), before);
    }

    #[test]
    fn transfer_can_overdraw_but_still_conserves() {
        // Going negative is meaningful state — it is what triggers an asset
        // sale — so it must not be clamped away.
        let mut e = economy();
        let before = e.total_cash();
        e.transfer(CorpId(0), CorpId(1), Fx::from_int(5000));
        assert!(e.corp(CorpId(0)).cash.is_negative());
        assert_eq!(e.total_cash(), before);
    }

    #[test]
    fn transfer_to_self_is_a_no_op() {
        let mut e = economy();
        e.transfer(CorpId(2), CorpId(2), Fx::from_int(100));
        assert_eq!(e.corp(CorpId(2)).cash, Fx::from_int(1000));
    }

    #[test]
    fn lane_ownership_moves_and_the_count_is_conserved() {
        let mut e = economy();
        let before = e.lane_count();
        e.set_lane_owner(LaneId(0), CorpId(3));
        assert_eq!(e.lane_count(), before);
        assert_eq!(e.lane_owner(LaneId(0)), CorpId(3));
        let total: usize = (0..4).map(|i| e.lanes_held(CorpId(i))).sum();
        assert_eq!(total, before);
    }

    #[test]
    fn gini_is_zero_when_holdings_are_equal() {
        assert_eq!(economy().lane_gini(), Fx::ZERO);
    }

    #[test]
    fn gini_rises_as_holdings_concentrate() {
        let mut e = economy();
        let equal = e.lane_gini();
        for lane in 0..12 {
            e.set_lane_owner(LaneId(lane), CorpId(0));
        }
        assert!(e.lane_gini() > equal);
        assert_eq!(e.dominance(), Fx::ONE);
    }

    #[test]
    fn overhead_odds_worsen_with_size() {
        let mut e = economy();
        let small = e.overhead_delay_odds(CorpId(0));
        for lane in 0..12 {
            e.set_lane_owner(LaneId(lane), CorpId(0));
        }
        // Lower denominator means a higher chance of delay.
        assert!(e.overhead_delay_odds(CorpId(0)) < small);
    }
}
