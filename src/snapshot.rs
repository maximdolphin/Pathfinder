//! World snapshot for the client.
//!
//! Design §5.3: snapshots are an optimisation and a presentation aid, **never a
//! source of truth**. The log is the truth; this is a rendering of the fold at
//! one tick, written for the UE5 client to read.
//!
//! Hand-rolled rather than pulled from a serialisation crate. The schema is
//! fixed and write-only, which is forty lines of `write!` — a dependency would
//! cost more in build time than it saves in code. When the contract needs to be
//! versioned and read back (Phase 1), this is replaced by generated protobuf
//! types from `proto/`, not by a JSON library.

use crate::ids::LaneId;
use crate::world::World;
use std::fmt::Write;

fn escaped(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    for ch in raw.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => {
                let _ = write!(out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out
}

pub fn to_json(world: &World) -> String {
    let mut out = String::with_capacity(64 * 1024);
    let _ = write!(
        out,
        "{{\n  \"schema\": 1,\n  \"seed\": {},\n  \"tick\": {},\n  \"eventCount\": {},\n",
        world.seed(),
        world.now().0,
        world.log().len()
    );

    // regions
    out.push_str("  \"regions\": [");
    for id in 0..world.region_count() {
        if id > 0 {
            out.push(',');
        }
        let _ = write!(
            out,
            "\n    {{ \"id\": {}, \"name\": \"{}\" }}",
            id,
            escaped(world.region_name(crate::ids::RegionId(id)))
        );
    }
    out.push_str("\n  ],\n");

    // corporations
    out.push_str("  \"corps\": [");
    for (index, corp) in world.economy().corps().iter().enumerate() {
        if index > 0 {
            out.push(',');
        }
        let _ = write!(
            out,
            "\n    {{ \"id\": {}, \"name\": \"{}\", \"cash\": \"{}\", \"cashRaw\": {}, \"lanes\": {}, \"home\": {} }}",
            corp.id.0,
            escaped(corp.name),
            corp.cash,
            corp.cash.to_raw(),
            world.economy().lanes_held(corp.id),
            corp.home.0
        );
    }
    out.push_str("\n  ],\n");

    // lane ownership — the conserved quantity the client can verify for itself
    out.push_str("  \"laneOwners\": [");
    for lane in 0..world.economy().lane_count() {
        if lane > 0 {
            out.push(',');
        }
        let _ = write!(out, "{}", world.economy().lane_owner(LaneId(lane as u32)).0);
    }
    out.push_str("],\n");

    // the contract board — what the client actually renders first
    out.push_str("  \"contracts\": [");
    let mut first = true;
    for contract in world.contracts() {
        if !first {
            out.push(',');
        }
        first = false;
        let _ = write!(
            out,
            "\n    {{ \"id\": {}, \"archetype\": \"{:?}\", \"poster\": \"{}\", \"target\": \"{}\", \"targetId\": {}, \"posted\": {}, \"tensionIncurred\": {}, \"resolved\": {} }}",
            contract.id.0,
            contract.archetype,
            escaped(world.economy().corp(contract.poster).name),
            escaped(&world.entity(contract.target).name),
            contract.target.0,
            contract.posted.0,
            contract.tension_incurred.0,
            contract.resolved
        );
    }
    out.push_str("\n  ],\n");

    // the open search volume, so the client can draw a region set and not a pin
    match world.investigation() {
        Some(investigation) => {
            let regions: Vec<String> = investigation
                .volume
                .regions()
                .map(|r| r.0.to_string())
                .collect();
            let _ = write!(
                out,
                "  \"investigation\": {{ \"contract\": {}, \"target\": \"{}\", \"inquiries\": {}, \"searchVolume\": [{}] }}\n",
                investigation.contract.0,
                escaped(&world.entity(investigation.target).name),
                investigation.inquiries,
                regions.join(", ")
            );
        }
        None => {
            out.push_str("  \"investigation\": null\n");
        }
    }

    out.push_str("}\n");
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn escaping_handles_the_characters_that_would_break_the_document() {
        assert_eq!(escaped("a\"b"), "a\\\"b");
        assert_eq!(escaped("a\\b"), "a\\\\b");
        assert_eq!(escaped("a\nb"), "a\\nb");
        assert_eq!(escaped("Kovač"), "Kovač", "UTF-8 passes through");
    }

    #[test]
    fn snapshot_is_balanced_and_non_trivial() {
        let mut world = World::genesis(9, 60);
        for _ in 0..200 {
            world.tick();
        }
        let json = to_json(&world);
        assert_eq!(
            json.matches('{').count(),
            json.matches('}').count(),
            "unbalanced braces"
        );
        assert_eq!(json.matches('[').count(), json.matches(']').count());
        assert!(json.contains("\"schema\": 1"));
        assert!(json.contains("laneOwners"));
    }
}
