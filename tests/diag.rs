use ledger::event::Proposition;
use ledger::world::World;

#[test]
#[ignore]
fn breakdown() {
    let mut w = World::genesis(20260908, 200);
    for _ in 0..50_000 { w.tick(); }
    let mut rows = std::collections::BTreeMap::new();
    for (_, b) in w.beliefs().all() {
        let kind = match b.proposition {
            Proposition::LocatedIn{..} => "LocatedIn",
            Proposition::Solvent{..} => "Solvent",
            Proposition::Insolvent{..} => "Insolvent",
            Proposition::ControlsLane{..} => "ControlsLane",
            Proposition::Skimming{..} => "Skimming",
            Proposition::BountyPosted{..} => "BountyPosted",
            Proposition::OwesObligation{..} => "OwesObligation",
        };
        let e = rows.entry(kind).or_insert((0usize, 0usize));
        e.0 += 1;
        if !w.is_true(&b.proposition) { e.1 += 1; }
    }
    println!("\n{:<16} {:>10} {:>10} {:>8}", "proposition", "held", "false", "pct");
    for (k, (n, f)) in &rows {
        println!("{:<16} {:>10} {:>10} {:>7}%", k, n, f, f*100/n.max(&1));
    }
    println!("total beliefs {}", w.beliefs().total_beliefs());
    let (res, fail) = w.resolution_record();
    println!("contracts resolved {} stale-location attempts {}", res, fail);
}

#[test]
#[ignore]
fn archetypes() {
    let mut w = World::genesis(20260908, 200);
    for _ in 0..50_000 { w.tick(); }
    let mut arch = std::collections::BTreeMap::new();
    for c in w.contracts() { *arch.entry(format!("{:?}", c.archetype)).or_insert(0usize) += 1; }
    println!("\ncontracts by archetype: {:?}", arch);
    let mut cls = std::collections::BTreeMap::new();
    for o in w.ledger().iter() { *cls.entry(format!("{:?}", o.class)).or_insert(0usize) += 1; }
    println!("open ledger by class:   {:?}", cls);
    let mut roles = std::collections::BTreeMap::new();
    for e in w.entities() { *roles.entry(format!("{:?}", e.role)).or_insert(0usize) += 1; }
    println!("population by role:     {:?}", roles);
}
