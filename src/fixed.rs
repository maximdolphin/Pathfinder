//! Fixed-point arithmetic.
//!
//! Design §5.2: **no floating point in authoritative state.** Every economic,
//! positional and confidence value in the sim is an `Fx` — a signed 64-bit
//! fixed-point number with six decimal places. This is what makes replay
//! bit-identical across machines, which is what makes Tier 4 testing possible.
//!
//! Multiply and divide go through `i128` so an intermediate product cannot
//! overflow silently.

use std::fmt;
use std::ops::{Add, AddAssign, Neg, Sub, SubAssign};

/// Six decimal places.
pub const SCALE: i64 = 1_000_000;

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Fx(i64);

impl Fx {
    pub const ZERO: Fx = Fx(0);
    pub const ONE: Fx = Fx(SCALE);

    /// Construct from the underlying scaled integer. Prefer the named
    /// constructors; this exists for serialisation round-trips.
    pub const fn raw(v: i64) -> Fx {
        Fx(v)
    }

    pub const fn to_raw(self) -> i64 {
        self.0
    }

    pub const fn from_int(v: i64) -> Fx {
        Fx(v * SCALE)
    }

    /// `Fx::permille(250)` is 0.25. The idiomatic way to write a confidence
    /// or a rate without reaching for a float literal.
    pub const fn permille(v: i64) -> Fx {
        Fx(v * (SCALE / 1000))
    }

    pub fn mul(self, other: Fx) -> Fx {
        Fx(((self.0 as i128 * other.0 as i128) / SCALE as i128) as i64)
    }

    pub fn div(self, other: Fx) -> Fx {
        assert!(other.0 != 0, "Fx::div by zero");
        Fx(((self.0 as i128 * SCALE as i128) / other.0 as i128) as i64)
    }

    /// Exact rational scaling — `x.scale_by(3, 4)` is three quarters of `x`,
    /// without the double rounding of `x.mul(Fx::permille(750))`.
    pub fn scale_by(self, num: i64, den: i64) -> Fx {
        assert!(den != 0, "Fx::scale_by by zero");
        Fx(((self.0 as i128 * num as i128) / den as i128) as i64)
    }

    pub fn clamp01(self) -> Fx {
        if self.0 < 0 {
            Fx::ZERO
        } else if self.0 > SCALE {
            Fx::ONE
        } else {
            self
        }
    }

    pub fn min(self, other: Fx) -> Fx {
        if self.0 <= other.0 {
            self
        } else {
            other
        }
    }

    pub fn max(self, other: Fx) -> Fx {
        if self.0 >= other.0 {
            self
        } else {
            other
        }
    }

    pub fn is_negative(self) -> bool {
        self.0 < 0
    }

    /// Truncates toward zero.
    pub fn trunc_int(self) -> i64 {
        self.0 / SCALE
    }
}

impl Add for Fx {
    type Output = Fx;
    fn add(self, o: Fx) -> Fx {
        Fx(self.0 + o.0)
    }
}

impl Sub for Fx {
    type Output = Fx;
    fn sub(self, o: Fx) -> Fx {
        Fx(self.0 - o.0)
    }
}

impl Neg for Fx {
    type Output = Fx;
    fn neg(self) -> Fx {
        Fx(-self.0)
    }
}

impl AddAssign for Fx {
    fn add_assign(&mut self, o: Fx) {
        self.0 += o.0;
    }
}

impl SubAssign for Fx {
    fn sub_assign(&mut self, o: Fx) {
        self.0 -= o.0;
    }
}

impl fmt::Display for Fx {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let magnitude = self.0.unsigned_abs();
        let int = magnitude / SCALE as u64;
        let frac = magnitude % SCALE as u64;
        let mut frac_str = format!("{:06}", frac);
        while frac_str.len() > 2 && frac_str.ends_with('0') {
            frac_str.pop();
        }
        write!(
            f,
            "{}{}.{}",
            if self.0 < 0 { "-" } else { "" },
            int,
            frac_str
        )
    }
}

impl fmt::Debug for Fx {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mul_and_div_round_trip() {
        let a = Fx::from_int(7);
        let b = Fx::permille(250);
        assert_eq!(a.mul(b), Fx::from_int(1) + Fx::permille(750));
        assert_eq!(a.mul(b).div(b), a);
    }

    #[test]
    fn mul_does_not_overflow_at_scale() {
        // 10^12 * 0.5 would overflow i64 at intermediate scale without the i128 widen.
        let big = Fx::from_int(1_000_000_000_000);
        assert_eq!(big.mul(Fx::permille(500)), Fx::from_int(500_000_000_000));
    }

    #[test]
    fn display_is_stable_and_signed() {
        assert_eq!(Fx::from_int(3).to_string(), "3.00");
        assert_eq!(Fx::permille(125).to_string(), "0.125");
        assert_eq!((-Fx::permille(125)).to_string(), "-0.125");
        assert_eq!(Fx::ZERO.to_string(), "0.00");
    }

    #[test]
    fn clamp01_bounds_confidence() {
        assert_eq!(Fx::from_int(5).clamp01(), Fx::ONE);
        assert_eq!(Fx::from_int(-5).clamp01(), Fx::ZERO);
        assert_eq!(Fx::permille(400).clamp01(), Fx::permille(400));
    }

    #[test]
    fn scale_by_avoids_double_rounding() {
        let x = Fx::raw(1_000_003);
        assert_eq!(x.scale_by(1, 3), Fx::raw(333_334));
    }
}
