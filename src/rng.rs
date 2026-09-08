//! Deterministic PRNG.
//!
//! Design §5.3: seeded per region per tick from `(world_seed, region_id, tick)`.
//! Never `thread_rng`. A tick's randomness must be reconstructible from the
//! world seed alone, or replay is not bit-identical and Tier 4 is a lie.
//!
//! Design §6.5 randomness policy: this seeds **when** and **where**, never
//! **what happens next**. Callers use it to pick timing and location; outcomes
//! are then derived deterministically from world state.

use crate::ids::{RegionId, Tick};

/// SplitMix64. Small, fast, no state array to serialise, and good enough for a
/// simulation that only needs timing jitter.
pub struct Rng {
    state: u64,
}

const GOLDEN: u64 = 0x9E37_79B9_7F4A_7C15;

fn mix(mut z: u64) -> u64 {
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

impl Rng {
    /// The only constructor that authoritative sim code may use.
    pub fn seeded(world_seed: u64, region: RegionId, tick: Tick) -> Rng {
        let mut s = mix(world_seed ^ (region.0 as u64).wrapping_mul(GOLDEN));
        s = mix(s ^ tick.0.wrapping_mul(0xBF58_476D_1CE4_E5B9));
        Rng { state: s }
    }

    /// For deriving a distinct stream within a tick — belief distortion is
    /// seeded by `(source, target, tick)` per §6.1, which this expresses.
    pub fn derive(&self, tag: u64) -> Rng {
        Rng {
            state: mix(self.state ^ tag.wrapping_mul(GOLDEN)),
        }
    }

    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(GOLDEN);
        mix(self.state)
    }

    /// Uniform in `0..n`. Modulo bias is bounded by `n / 2^64` and every `n`
    /// in this sim is under a thousand, so the bias is below 2^-54 — far
    /// beneath anything the simulation can express.
    pub fn below(&mut self, n: u64) -> u64 {
        assert!(n > 0, "Rng::below(0)");
        self.next_u64() % n
    }

    pub fn chance(&mut self, numerator: u64, denominator: u64) -> bool {
        self.below(denominator) < numerator
    }

    pub fn pick<'a, T>(&mut self, items: &'a [T]) -> Option<&'a T> {
        if items.is_empty() {
            None
        } else {
            let i = self.below(items.len() as u64) as usize;
            Some(&items[i])
        }
    }

    /// Deterministic shuffle. Needed because iterating a `HashMap` is not
    /// order-stable and any ordering that reaches authoritative state must be
    /// derived from the seed instead.
    pub fn shuffle<T>(&mut self, items: &mut [T]) {
        for i in (1..items.len()).rev() {
            let j = self.below(i as u64 + 1) as usize;
            items.swap(i, j);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rng(tick: u64) -> Rng {
        Rng::seeded(42, RegionId(1), Tick(tick))
    }

    #[test]
    fn same_seed_same_sequence() {
        let a: Vec<u64> = (0..16).map(|_| rng(7).next_u64()).collect();
        let b: Vec<u64> = (0..16).map(|_| rng(7).next_u64()).collect();
        assert_eq!(a, b);
    }

    #[test]
    fn adjacent_ticks_do_not_correlate() {
        let a = rng(7).next_u64();
        let b = rng(8).next_u64();
        assert_ne!(a, b);
    }

    #[test]
    fn adjacent_regions_do_not_correlate() {
        let a = Rng::seeded(42, RegionId(1), Tick(7)).next_u64();
        let b = Rng::seeded(42, RegionId(2), Tick(7)).next_u64();
        assert_ne!(a, b);
    }

    #[test]
    fn below_stays_in_range() {
        let mut r = rng(3);
        for _ in 0..1000 {
            assert!(r.below(5) < 5);
        }
    }

    #[test]
    fn shuffle_is_a_permutation_and_deterministic() {
        let mut a: Vec<u32> = (0..32).collect();
        let mut b = a.clone();
        rng(11).shuffle(&mut a);
        rng(11).shuffle(&mut b);
        assert_eq!(a, b);
        a.sort();
        assert_eq!(a, (0..32).collect::<Vec<u32>>());
    }
}
