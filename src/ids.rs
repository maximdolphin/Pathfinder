//! Typed identifiers.
//!
//! Design §8.2: no magic strings, and no bare integers standing in for
//! identity either. A `CorpId` cannot be passed where an `EntityId` is
//! expected, which is a whole class of bug the compiler removes for free.

use std::fmt;

macro_rules! id {
    ($name:ident, $inner:ty, $prefix:literal) => {
        #[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
        pub struct $name(pub $inner);

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, concat!($prefix, "{}"), self.0)
            }
        }

        impl fmt::Debug for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                fmt::Display::fmt(self, f)
            }
        }
    };
}

id!(EntityId, u32, "e");
id!(CorpId, u32, "c");
id!(RegionId, u32, "r");
id!(LaneId, u32, "l");
id!(ContractId, u32, "k");
id!(EventId, u32, "#");

/// Simulation time. One tick is nominally an hour of world time; nothing in
/// the sim depends on that, it is a presentation concern.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Default)]
pub struct Tick(pub u64);

impl Tick {
    pub fn since(self, earlier: Tick) -> u64 {
        self.0.saturating_sub(earlier.0)
    }
}

impl fmt::Display for Tick {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "t{}", self.0)
    }
}

impl fmt::Debug for Tick {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(self, f)
    }
}
