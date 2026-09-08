//! World state and the fold. Design §5.3.
//!
//! `State_n = fold(reduce, State_0, events[0..n])`.
//!
//! Every field below is private. The only mutating entry point is `apply`, and
//! the only way to reach `apply` is `emit`, which appends to the log first.
//! That is the "no other write path" rule made structural rather than
//! documented — you cannot violate it without editing this module.
//!
//! `tick` never mutates. It *decides* — reading state and the seeded PRNG to
//! produce a list of events — and then emits them. Keeping decision and
//! mutation apart is what makes replay a pure re-fold of the log.

use crate::belief::{distort, transmitted_confidence, Belief, BeliefStore};
use crate::economy::{Corp, Economy, Shipment, ShipmentState};
use crate::event::{
    Archetype, DelayCause, Event, EventLog, LoggedEvent, Proposition, Provenance,
};
use crate::fixed::Fx;
use crate::ids::{ContractId, CorpId, EntityId, EventId, LaneId, RegionId, Tick};
use crate::investigation::{will_disclose, Incentive, SearchVolume};
use crate::mission::{find_tension, MissionContract};
use crate::obligation::{Ledger, Obligation, ObligationClass};
use crate::rng::Rng;

// Distinct PRNG streams within a tick, so adding a subsystem cannot shift the
// random sequence another subsystem sees.
const STREAM_ECONOMY: u64 = 1;
const STREAM_TALK: u64 = 2;
const STREAM_AGENT: u64 = 4;
const STREAM_OBSERVE: u64 = 5;
const STREAM_FAVOUR: u64 = 6;

const SHIPMENT_TRANSIT_TICKS: u64 = 18;
/// How many recent contract targets the mission query refuses to reuse.
const RECENT_TARGET_MEMORY: usize = 45;
/// Minimum ticks between one target's relocations. Without a floor here an
/// alarmed target moves every tick — the alarm belief does not go away just
/// because they ran — and the entire belief graph is invalidated continuously.
const FLIGHT_COOLDOWN: u64 = 400;
/// How many bystanders notice a lane changing hands.
const SALE_WITNESS_LIMIT: usize = 4;
/// Ticks between the agent's inquiries.
///
/// A contractor asks a few people over a few days, not one person every hour.
/// Acting every tick generated thousands of "someone is looking for X" beliefs
/// per contract, swamping every other proposition in the world and leaving the
/// target no quiet stretch in which to decide to run. §4.1 budgets three to six
/// information-gathering interactions per session; this is that, in ticks.
const INQUIRY_INTERVAL: u64 = 24;
const MISSION_QUERY_INTERVAL: u64 = 140;
/// Confidence at which a target believes the rumour about itself and moves.
const FLIGHT_THRESHOLD: Fx = Fx::permille(450);

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Role {
    Dockworker,
    PortAuthority,
    Executive,
    Fixer,
    Analyst,
    Courier,
}

impl Role {
    /// What this role can be leaned on for. Drives which obligation classes
    /// exist between which people, which in turn drives which archetypes the
    /// mission query can find.
    /// Deliberately balanced two-to-two-to-two across the three archetypes
    /// (§6.6). Roles are drawn uniformly, so a skew here becomes a skew in the
    /// contract board — the first version mapped four of six roles onto
    /// Audit-producing classes and two thirds of all contracts came out Audits
    /// before the mission query did anything at all.
    pub fn typical_obligation(self) -> ObligationClass {
        match self {
            // -> Audit
            Role::Dockworker => ObligationClass::Manifest,
            Role::Analyst => ObligationClass::CleanLanding,
            // -> Bounty
            Role::Executive => ObligationClass::Silence,
            Role::Fixer => ObligationClass::Silence,
            // -> Collection
            Role::PortAuthority => ObligationClass::Debt,
            Role::Courier => ObligationClass::Debt,
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Role::Dockworker => "dockworker",
            Role::PortAuthority => "port authority",
            Role::Executive => "executive",
            Role::Fixer => "fixer",
            Role::Analyst => "analyst",
            Role::Courier => "courier",
        }
    }
}

#[derive(Clone, Debug)]
pub struct Entity {
    pub id: EntityId,
    pub name: String,
    pub role: Role,
    pub region: RegionId,
    pub employer: Option<CorpId>,
}

/// The scripted contractor's current job. Design §6.3 state, kept beside the
/// world rather than inside it — the sim does not privilege the agent.
#[derive(Clone, Debug)]
pub struct Investigation {
    pub contract: ContractId,
    pub target: EntityId,
    pub volume: SearchVolume,
    pub inquiries: u32,
    pub sources_used: Vec<EntityId>,
}

/// An event the tick has decided on but not yet committed.
#[derive(Clone, Copy)]
struct Intent {
    region: RegionId,
    cause: Option<EventId>,
    /// Take the previous committed event in this batch as the cause. A cascade
    /// cannot name its own predecessor's id up front — the id does not exist
    /// until the predecessor is committed — so the batch resolves it.
    chain_to_previous: bool,
    event: Event,
}

impl Intent {
    fn new(region: RegionId, event: Event) -> Intent {
        Intent {
            region,
            cause: None,
            chain_to_previous: false,
            event,
        }
    }

    fn caused_by(region: RegionId, cause: Option<EventId>, event: Event) -> Intent {
        Intent {
            region,
            cause,
            chain_to_previous: false,
            event,
        }
    }

    fn chained(region: RegionId, event: Event) -> Intent {
        Intent {
            region,
            cause: None,
            chain_to_previous: true,
            event,
        }
    }
}

pub struct World {
    seed: u64,
    now: Tick,
    region_count: u32,
    region_names: Vec<&'static str>,
    entities: Vec<Entity>,
    social: Vec<Vec<EntityId>>,
    beliefs: BeliefStore,
    ledger: Ledger,
    economy: Economy,
    contracts: Vec<MissionContract>,
    log: EventLog,
    agent: EntityId,
    investigation: Option<Investigation>,
    /// Which event caused a shipment to be late, so the penalty can point at it.
    /// Derived bookkeeping, maintained inside the fold like everything else.
    delay_event: Vec<Option<EventId>>,
    resolved_contracts: u32,
    failed_resolutions: u32,
    last_flight: Tick,
}

const REGIONS: [&str; 6] = [
    "Corvid Station",
    "Halberd Yards",
    "Tessellate Reach",
    "Ninth Ward Docks",
    "Auger Flats",
    "The Understack",
];

const CORP_NAMES: [&str; 5] = [
    "Aurel Holdings",
    "Kestrel Freight Consortium",
    "Vantage Terraform",
    "Ostrander Compliance",
    "Meridian Lien Group",
];

const GIVEN: [&str; 20] = [
    "Ilse", "Novak", "Ambrose", "Teodora", "Sable", "Wren", "Cassius", "Odile", "Marek", "Fenn",
    "Ruth", "Absalom", "Ivy", "Corvin", "Delphine", "Osric", "Tamsin", "Bram", "Lucia", "Edric",
];

const FAMILY: [&str; 20] = [
    "Vance", "Okonkwo", "Halloran", "Reyes", "Strand", "Petrosyan", "Achebe", "Lindqvist",
    "Amari", "Duquesne", "Bellweather", "Nakamura", "Orsini", "Kovač", "Ashgrove", "Mbeki",
    "Larkin", "Vashti", "Toomey", "Ferris",
];

const ROLES: [Role; 6] = [
    Role::Dockworker,
    Role::PortAuthority,
    Role::Executive,
    Role::Fixer,
    Role::Analyst,
    Role::Courier,
];

impl World {
    /// Genesis. Everything here is derived from the seed, so two worlds built
    /// from the same seed are identical before a single tick runs.
    pub fn genesis(seed: u64, entity_count: usize) -> World {
        let region_count = REGIONS.len() as u32;
        let mut rng = Rng::seeded(seed, RegionId(0), Tick(0));

        let corps: Vec<Corp> = CORP_NAMES
            .iter()
            .enumerate()
            .map(|(i, name)| Corp {
                id: CorpId(i as u32),
                name,
                cash: Fx::from_int(4000),
                home: RegionId(i as u32 % region_count),
            })
            .collect();

        // 30 lanes dealt round-robin: the world starts balanced and has to
        // concentrate on its own, so any Gini trend is the sim's doing.
        let lane_owner: Vec<CorpId> = (0..30).map(|i| CorpId(i % corps.len() as u32)).collect();

        let mut entities = Vec::with_capacity(entity_count);
        for i in 0..entity_count {
            let given = GIVEN[rng.below(GIVEN.len() as u64) as usize];
            let family = FAMILY[rng.below(FAMILY.len() as u64) as usize];
            let role = ROLES[rng.below(ROLES.len() as u64) as usize];
            let employer = if rng.chance(4, 5) {
                Some(CorpId(rng.below(corps.len() as u64) as u32))
            } else {
                None
            };
            entities.push(Entity {
                id: EntityId(i as u32),
                name: format!("{} {}", given, family),
                role,
                region: RegionId(rng.below(region_count as u64) as u32),
                employer,
            });
        }

        // Social graph. Mostly local: people talk to people they can see, so
        // most edges stay inside a region and information has to *travel* to
        // cross the map. A uniform random graph over this population puts
        // everybody within two hops of everybody, which collapses propagation
        // depth to one and quietly hands the world omniscience.
        //
        // A ring underneath guarantees connectivity, so nobody is stranded and
        // the graph is densely cyclic — propagation must terminate on it, which
        // is asserted in the invariant suite.
        let mut social = vec![Vec::new(); entity_count];
        let mut connect = |social: &mut Vec<Vec<EntityId>>, a: usize, b: usize| {
            if a == b {
                return;
            }
            if !social[a].contains(&EntityId(b as u32)) {
                social[a].push(EntityId(b as u32));
                social[b].push(EntityId(a as u32));
            }
        };

        for i in 0..entity_count {
            connect(&mut social, i, (i + 1) % entity_count);
        }
        for i in 0..entity_count {
            let region = entities[i].region;
            let locals: Vec<usize> = (0..entity_count)
                .filter(|j| *j != i && entities[*j].region == region)
                .collect();
            for _ in 0..2 {
                if let Some(&j) = rng.pick(&locals) {
                    connect(&mut social, i, j);
                }
            }
            // A long-distance contact for one in six. Enough for news to cross
            // the map eventually, not enough for it to arrive intact — and few
            // enough that crossing actually costs hops.
            if rng.chance(1, 6) {
                let j = rng.below(entity_count as u64) as usize;
                connect(&mut social, i, j);
            }
        }

        let agent = EntityId(0);

        let mut world = World {
            seed,
            now: Tick(0),
            region_count,
            region_names: REGIONS.to_vec(),
            beliefs: BeliefStore::with_capacity(entity_count),
            social,
            entities,
            ledger: Ledger::new(),
            economy: Economy::new(corps, lane_owner),
            contracts: Vec::new(),
            log: EventLog::new(),
            agent,
            investigation: None,
            delay_event: Vec::new(),
            resolved_contracts: 0,
            failed_resolutions: 0,
            last_flight: Tick(0),
        };

        world.seed_initial_obligations(&mut rng);
        world
    }

    fn seed_initial_obligations(&mut self, rng: &mut Rng) {
        // A world that starts with no history has no tension to query, and the
        // first hundred contracts would all be manufactured. Pre-existing debt
        // is the raw material §6.6 needs.
        let count = self.entities.len() / 3;
        for _ in 0..count {
            let debtor = EntityId(rng.below(self.entities.len() as u64) as u32);
            let creditor = EntityId(rng.below(self.entities.len() as u64) as u32);
            if debtor == creditor {
                continue;
            }
            let class = self.entities[creditor.0 as usize].role.typical_obligation();
            let region = self.entities[debtor.0 as usize].region;
            self.commit(Intent::new(
                region,
                Event::ObligationIncurred {
                    debtor,
                    creditor,
                    class,
                    weight: Fx::permille(200 + rng.below(700) as i64),
                    public: rng.chance(1, 3),
                },
            ));
        }
    }

    // ---- the single write path -----------------------------------------

    /// Commits a batch in order, resolving `chain_to_previous` as it goes.
    fn commit_all(&mut self, intents: Vec<Intent>) {
        let mut previous: Option<EventId> = None;
        for intent in intents {
            let cause = if intent.chain_to_previous {
                previous
            } else {
                intent.cause
            };
            previous = Some(self.commit(Intent { cause, ..intent }));
        }
    }

    fn commit(&mut self, intent: Intent) -> EventId {
        let id = self
            .log
            .append(self.now, intent.region, intent.cause, intent.event);
        let logged = *self
            .log
            .get(id)
            .expect("event was just appended and must be readable");
        self.apply(&logged);
        id
    }

    /// The reducer. Exhaustive over `Event` — adding a variant without handling
    /// it here is a compile error, which is the §8.4 discipline the type system
    /// is here to enforce.
    fn apply(&mut self, logged: &LoggedEvent) {
        match logged.event {
            Event::ShipmentDispatched { contract } => {
                self.economy
                    .set_shipment_state(contract, ShipmentState::InTransit);
            }

            Event::ShipmentDelayed { contract, .. } => {
                self.economy
                    .set_shipment_state(contract, ShipmentState::Delayed);
                self.delay_event[contract.0 as usize] = Some(logged.id);
            }

            Event::ShipmentDelivered { contract } => {
                self.economy
                    .set_shipment_state(contract, ShipmentState::Delivered);
            }

            Event::CashTransferred { from, to, amount } => {
                self.economy.transfer(from, to, amount);
            }

            Event::PenaltyTriggered { contract, .. } => {
                self.economy
                    .set_shipment_state(contract, ShipmentState::Defaulted);
            }

            // Pure signal: the state that matters (negative cash) is already
            // true by the time this is emitted. It exists so the causal chain
            // has a node a reader can name.
            Event::ReservesWentNegative { .. } => {}

            Event::LaneSold { lane, to, .. } => {
                self.economy.set_lane_owner(lane, to);
            }

            Event::Witnessed { who, proposition } => {
                debug_assert_eq!(
                    self.entities[who.0 as usize].region, logged.region,
                    "§6.1 invariant: a witness must have been in the region at the event tick"
                );
                self.beliefs.record(
                    who,
                    Belief {
                        proposition,
                        confidence: Fx::permille(950),
                        acquired_tick: logged.tick,
                        provenance: Provenance::Witnessed,
                        hops: 0,
                    },
                );
            }

            Event::Told {
                from,
                to,
                proposition,
                confidence,
            } => {
                let hops = self
                    .beliefs
                    .belief_about(from, |p| *p == proposition)
                    .map(|b| b.hops.saturating_add(1))
                    .unwrap_or(1);
                self.beliefs.record(
                    to,
                    Belief {
                        proposition,
                        confidence,
                        acquired_tick: logged.tick,
                        provenance: Provenance::Told(from),
                        hops,
                    },
                );
            }

            Event::Fabricated { by, to, proposition } => {
                self.beliefs.record(
                    to,
                    Belief {
                        proposition,
                        confidence: Fx::permille(700),
                        acquired_tick: logged.tick,
                        provenance: Provenance::Fabricated(by),
                        hops: 1,
                    },
                );
            }

            Event::ObligationIncurred {
                debtor,
                creditor,
                class,
                weight,
                public,
            } => {
                self.ledger.incur(Obligation {
                    debtor,
                    creditor,
                    class,
                    weight,
                    incurred: logged.tick,
                    expiry: None,
                    public,
                });
            }

            Event::ObligationRedeemed {
                debtor,
                creditor,
                class,
            } => {
                self.ledger.redeem(debtor, creditor, class);
            }

            Event::ContractPosted {
                contract,
                archetype,
                poster,
                target,
            } => {
                // `tension_incurred` is read from the ledger rather than passed
                // in, so a contract cannot claim a history it does not have.
                let incurred = self
                    .ledger
                    .owed_by(target)
                    .first()
                    .map(|o| o.incurred)
                    .unwrap_or(logged.tick);
                self.contracts.push(MissionContract {
                    id: contract,
                    archetype,
                    poster,
                    target,
                    posted: logged.tick,
                    tension_incurred: incurred,
                    resolved: false,
                });
            }

            Event::InquiryMade { .. } => {}

            Event::TargetRelocated { who, to, .. } => {
                self.entities[who.0 as usize].region = to;
            }

            Event::ContractResolved { contract, .. } => {
                if let Some(c) = self
                    .contracts
                    .iter_mut()
                    .find(|c| c.id == contract)
                {
                    c.resolved = true;
                }
            }
        }
    }

    // ---- the tick -------------------------------------------------------

    pub fn tick(&mut self) {
        self.now = Tick(self.now.0 + 1);
        self.beliefs.decay_all();
        self.ledger.expire(self.now);

        let economy = self.decide_economy();
        self.commit_all(economy);
        let observed = self.decide_observation();
        self.commit_all(observed);
        let favours = self.decide_favours();
        self.commit_all(favours);
        let redemptions = self.decide_redemptions();
        self.commit_all(redemptions);
        let talk = self.decide_talk();
        self.commit_all(talk);
        let missions = self.decide_missions();
        self.commit_all(missions);
        self.step_agent();
        self.step_flight();
    }

    /// People notice who else is standing in the room.
    ///
    /// This is the substrate the investigation algorithm consumes. Without it
    /// the mission query has no vulnerable debtors to find, because
    /// "vulnerable" means "somebody holds a belief about them" — and a world
    /// where nobody has noticed anybody is a world with no leverage in it.
    ///
    /// Deliberately sparse and uniform: one observation per tick across the
    /// whole population. Making it dense hands the player omniscience by the
    /// back door and kills pillar P3 — at three per tick the belief graph was
    /// so saturated with first-hand sightings that nothing ever propagated far
    /// enough to distort.
    fn decide_observation(&mut self) -> Vec<Intent> {
        let mut rng = self.rng(STREAM_OBSERVE);
        let mut intents = Vec::new();
        if self.now.0 % 2 != 0 {
            return intents;
        }
        for _ in 0..1 {
            let observer = EntityId(rng.below(self.entities.len() as u64) as u32);
            let region = self.entities[observer.0 as usize].region;
            let locals: Vec<EntityId> = self
                .entities
                .iter()
                .filter(|e| e.region == region && e.id != observer)
                .map(|e| e.id)
                .collect();
            if let Some(&seen) = rng.pick(&locals) {
                intents.push(Intent::new(
                    region,
                    Event::Witnessed {
                        who: observer,
                        proposition: Proposition::LocatedIn { who: seen, region },
                    },
                ));
            }

            // Insiders can see their own employer's books. This is the only
            // organic source of *true* institutional belief in the world —
            // without it every such belief traces back to a fixer's lie or a
            // distressed sale, and the false-belief share cannot come down no
            // matter how the rest is tuned.
            if rng.chance(1, 3) {
            if let Some(employer) = self.entities[observer.0 as usize].employer {
                let solvent = !self.economy.corp(employer).cash.is_negative();
                intents.push(Intent::new(
                    region,
                    Event::Witnessed {
                        who: observer,
                        proposition: if solvent {
                            Proposition::Solvent { corp: employer }
                        } else {
                            Proposition::Insolvent { corp: employer }
                        },
                    },
                ));
            }
            }
        }
        intents
    }

    /// People in the same room do each other favours.
    ///
    /// Without this the ledger is stocked once at genesis, drains, and the only
    /// obligations left are the ones the agent earned by closing contracts —
    /// which are all the same class, so every subsequent contract comes out the
    /// same archetype. A world where nobody incurs a debt has no tension to
    /// query, and §6.6 is only as varied as the ledger underneath it.
    fn decide_favours(&mut self) -> Vec<Intent> {
        let mut rng = self.rng(STREAM_FAVOUR);
        if !rng.chance(1, 5) {
            return Vec::new();
        }
        let debtor = EntityId(rng.below(self.entities.len() as u64) as u32);
        let region = self.entities[debtor.0 as usize].region;
        let locals: Vec<EntityId> = self
            .entities
            .iter()
            .filter(|e| e.region == region && e.id != debtor)
            .map(|e| e.id)
            .collect();
        let creditor = match rng.pick(&locals) {
            Some(c) => *c,
            None => return Vec::new(),
        };
        // The class follows from what the creditor is in a position to ask for,
        // which is what makes the archetype mix a property of the population
        // rather than of a table of weights.
        let class = self.entities[creditor.0 as usize].role.typical_obligation();
        vec![Intent::new(
            region,
            Event::ObligationIncurred {
                debtor,
                creditor,
                class,
                weight: Fx::permille(200 + rng.below(700) as i64),
                public: rng.chance(1, 3),
            },
        )]
    }

    /// NPCs call in favours on each other, not only on the player.
    ///
    /// §6.2 says favours are consumed on redemption and that NPCs redeeming
    /// them is a primary mission source, "not optional content". A ledger that
    /// only ever grows is a ledger nobody is using, and the §12 redemption-rate
    /// band exists precisely to catch that.
    fn decide_redemptions(&mut self) -> Vec<Intent> {
        let mut rng = self.rng(STREAM_FAVOUR).derive(0x5EED);
        // Slower than creation (one in five), so the ledger carries a standing
        // stock of open obligations rather than clearing as fast as it fills.
        // §12 wants 40-70% redeemed: too low and favours feel worthless, too
        // high and they are trivially farmed.
        if !rng.chance(1, 9) {
            return Vec::new();
        }
        let open: Vec<(EntityId, EntityId, ObligationClass)> = self
            .ledger
            .iter()
            .filter(|o| o.creditor != self.agent)
            .map(|o| (o.debtor, o.creditor, o.class))
            .collect();
        match rng.pick(&open) {
            Some(&(debtor, creditor, class)) => vec![Intent::new(
                self.entities[creditor.0 as usize].region,
                Event::ObligationRedeemed {
                    debtor,
                    creditor,
                    class,
                },
            )],
            None => Vec::new(),
        }
    }

    fn rng(&self, stream: u64) -> Rng {
        Rng::seeded(self.seed, RegionId(0), self.now).derive(stream)
    }

    /// The §6.5 causal spine, and the reason the dump is readable:
    ///
    /// > a shipment is late (random *timing*) → contract penalty (deterministic)
    /// > → reserves go negative (deterministic) → an asset is sold to a rival
    /// > (deterministic) → the rival controls a lane (deterministic).
    ///
    /// Randomness picks when and where. It never picks the outcome.
    fn decide_economy(&mut self) -> Vec<Intent> {
        let mut rng = self.rng(STREAM_ECONOMY);
        let mut intents = Vec::new();

        if rng.chance(1, 3) {
            let shipper = CorpId(rng.below(self.economy.corps().len() as u64) as u32);
            let client = CorpId(rng.below(self.economy.corps().len() as u64) as u32);
            let lanes = self.economy.lanes_of(shipper);
            if shipper != client && !lanes.is_empty() {
                let lane = lanes[rng.below(lanes.len() as u64) as usize];
                let id = self.economy.add_shipment(Shipment {
                    id: ContractId(0),
                    shipper,
                    client,
                    lane,
                    dispatched: self.now,
                    due: Tick(self.now.0 + SHIPMENT_TRANSIT_TICKS),
                    penalty: Fx::from_int(60 + rng.below(140) as i64)
                        .scale_by(10 + self.economy.lanes_held(shipper) as i64, 10),
                    state: ShipmentState::InTransit,
                });
                self.delay_event.push(None);
                let home = self.economy.corp(shipper).home;
                intents.push(Intent::new(
                    home,
                    Event::ShipmentDispatched { contract: id },
                ));
            }
        }

        let live: Vec<Shipment> = self
            .economy
            .shipments()
            .iter()
            .filter(|s| {
                matches!(s.state, ShipmentState::InTransit | ShipmentState::Delayed)
            })
            .copied()
            .collect();

        for shipment in live {
            let home = self.economy.corp(shipment.shipper).home;

            if shipment.state == ShipmentState::InTransit {
                let odds = self.economy.overhead_delay_odds(shipment.shipper);
                if rng.chance(1, odds) {
                    let cause = if self.economy.lanes_held(shipment.shipper) > 8 {
                        DelayCause::OperatorOverhead
                    } else if rng.chance(1, 2) {
                        DelayCause::LaneCongestion
                    } else {
                        DelayCause::CrewShortage
                    };
                    intents.push(Intent::new(
                        home,
                        Event::ShipmentDelayed {
                            contract: shipment.id,
                            cause,
                        },
                    ));
                    continue;
                }
                if self.now >= shipment.due {
                    intents.push(Intent::new(
                        home,
                        Event::ShipmentDelivered {
                            contract: shipment.id,
                        },
                    ));
                }
                continue;
            }

            // Delayed, and now past due: the penalty is deterministic.
            if self.now >= shipment.due {
                intents.extend(self.decide_default_chain(shipment));
            }
        }

        intents
    }

    /// Everything downstream of a missed delivery window. No randomness reaches
    /// this function — given the same late shipment it always produces the same
    /// consequences, which is what makes the chain legible in hindsight (P2).
    fn decide_default_chain(&self, shipment: Shipment) -> Vec<Intent> {
        let mut intents = Vec::new();
        let home = self.economy.corp(shipment.shipper).home;
        let delay = self.delay_event[shipment.id.0 as usize];

        intents.push(Intent::caused_by(
            home,
            delay,
            Event::PenaltyTriggered {
                contract: shipment.id,
                debtor: shipment.shipper,
                creditor: shipment.client,
                amount: shipment.penalty,
            },
        ));
        intents.push(Intent::chained(
            home,
            Event::CashTransferred {
                from: shipment.shipper,
                to: shipment.client,
                amount: shipment.penalty,
            },
        ));

        let after_penalty = self.economy.corp(shipment.shipper).cash - shipment.penalty;
        if !after_penalty.is_negative() {
            return intents;
        }

        intents.push(Intent::chained(
            home,
            Event::ReservesWentNegative {
                corp: shipment.shipper,
            },
        ));

        // Sell a lane to the best-capitalised rival that is not already
        // dominant. The cap is the compliance division of §3 doing its job, and
        // it makes "no faction exceeds 40%" structural rather than hoped for
        // (§8.4: handle the edge case with the shape of the solution).
        let cap = self.economy.lane_count() * 2 / 5;
        let lanes = self.economy.lanes_of(shipment.shipper);
        let buyer = self
            .economy
            .corps()
            .iter()
            .filter(|c| c.id != shipment.shipper)
            .filter(|c| self.economy.lanes_held(c.id) < cap)
            .max_by_key(|c| (c.cash, std::cmp::Reverse(c.id)))
            .map(|c| c.id);

        if let (Some(lane), Some(buyer)) = (lanes.first().copied(), buyer) {
            let price = Fx::from_int(400);
            intents.push(Intent::chained(
                home,
                Event::LaneSold {
                    lane,
                    from: shipment.shipper,
                    to: buyer,
                    price,
                },
            ));
            intents.push(Intent::chained(
                home,
                Event::CashTransferred {
                    from: buyer,
                    to: shipment.shipper,
                    amount: price,
                },
            ));
            // A few people standing in the region see the transfer of control.
            // Not everyone: witnessing this with the whole population produced
            // tens of thousands of ownership claims, most of them about a lane
            // that had since changed hands again, and the false-belief share
            // ran away on that alone. These are observations of the sale, not
            // further links in the cascade, so they do not extend the chain.
            let present: Vec<EntityId> = self
                .entities
                .iter()
                .filter(|e| e.region == home)
                .map(|e| e.id)
                .take(SALE_WITNESS_LIMIT)
                .collect();
            for who in present {
                intents.push(Intent::new(
                    home,
                    Event::Witnessed {
                        who,
                        proposition: Proposition::ControlsLane { corp: buyer, lane },
                    },
                ));
            }
        }

        intents
    }

    /// Belief propagation (§6.1): latency, decay, distortion. Single-hop per
    /// tick per speaker, which is what makes termination on a cyclic social
    /// graph structural rather than a guard clause.
    fn decide_talk(&mut self) -> Vec<Intent> {
        let mut rng = self.rng(STREAM_TALK);
        let mut intents = Vec::new();

        for index in 0..self.entities.len() {
            let speaker = EntityId(index as u32);
            let mut stream = rng.derive(index as u64);
            if !stream.chance(1, 3) {
                continue;
            }

            let neighbours = &self.social[index];
            let listener = match stream.pick(neighbours) {
                Some(l) => *l,
                None => continue,
            };

            let held = self.beliefs.held_by(speaker);
            let belief = match stream.pick(held) {
                Some(b) => *b,
                None => continue,
            };

            let confidence = transmitted_confidence(belief.confidence);
            if confidence < Fx::permille(30) {
                continue;
            }

            let proposition = distort(belief.proposition, &mut stream, self.region_count);
            intents.push(Intent::new(
                self.entities[index].region,
                Event::Told {
                    from: speaker,
                    to: listener,
                    proposition,
                    confidence,
                },
            ));
        }

        // Fixers occasionally plant something. Same pipeline as the truth —
        // only the provenance differs (§6.1).
        if rng.chance(1, 150) {
            let fixers: Vec<EntityId> = self
                .entities
                .iter()
                .filter(|e| e.role == Role::Fixer)
                .map(|e| e.id)
                .collect();
            if let Some(&fixer) = rng.pick(&fixers) {
                let neighbours = &self.social[fixer.0 as usize];
                if let Some(&listener) = rng.pick(neighbours) {
                    let corp = CorpId(rng.below(self.economy.corps().len() as u64) as u32);
                    intents.push(Intent::new(
                        self.entities[fixer.0 as usize].region,
                        Event::Fabricated {
                            by: fixer,
                            to: listener,
                            proposition: Proposition::Insolvent { corp },
                        },
                    ));
                }
            }
        }

        intents
    }

    fn decide_missions(&mut self) -> Vec<Intent> {
        if self.now.0 % MISSION_QUERY_INTERVAL != 0 {
            return Vec::new();
        }
        if self.contracts.iter().any(|c| !c.resolved) {
            return Vec::new();
        }

        let beliefs = &self.beliefs;
        let entities = &self.entities;
        let agent = self.agent;

        // Debtors targeted recently are off the table. Without this the query
        // returns the oldest tension every time, the same person is hunted
        // forever, and every contract comes out the same archetype — the
        // oatmeal problem arriving through the front door. Excluding recent
        // targets rotates the query across the ledger, and the archetype
        // variety follows from the obligation classes it reaches.
        let recent: Vec<EntityId> = self
            .contracts
            .iter()
            .rev()
            .take(RECENT_TARGET_MEMORY)
            .map(|c| c.target)
            .collect();

        let tension = find_tension(
            &self.ledger,
            self.now,
            |e| entities[e.0 as usize].employer,
            // Vulnerable means somebody, somewhere, holds a belief about them.
            // A person nobody has heard of cannot be leveraged.
            |e| {
                e != agent
                    && !recent.contains(&e)
                    && beliefs
                        .all()
                        .any(|(_, b)| b.proposition.entity_subject() == Some(e))
            },
        );

        let tension = match tension {
            Some(t) => t,
            None => return Vec::new(),
        };

        let region = self.entities[t_region(&self.entities, tension.debtor)].region;
        let mut intents = vec![Intent::new(
            region,
            Event::ContractPosted {
                contract: ContractId(self.contracts.len() as u32),
                archetype: tension.archetype(),
                poster: tension.creditor_corp,
                target: tension.debtor,
            },
        )];

        // A posted contract makes people look. Whoever is standing near the
        // target notices where they are; that knowledge then decays, distorts
        // and spreads like any other, which is what gives the investigation
        // sources that are sometimes stale and sometimes wrong.
        for entity in self
            .entities
            .iter()
            .filter(|e| e.region == region && e.id != tension.debtor)
        {
            intents.push(Intent::new(
                region,
                Event::Witnessed {
                    who: entity.id,
                    proposition: Proposition::LocatedIn {
                        who: tension.debtor,
                        region,
                    },
                },
            ));
        }

        intents
    }

    /// The scripted agent of §13.1 — enough behaviour to exercise §6.3 headless.
    fn step_agent(&mut self) {
        if self.investigation.is_none() {
            if let Some(contract) = self.contracts.iter().find(|c| !c.resolved) {
                self.investigation = Some(Investigation {
                    contract: contract.id,
                    target: contract.target,
                    volume: SearchVolume::full(self.region_count),
                    inquiries: 0,
                    sources_used: Vec::new(),
                });
            }
            return;
        }

        if self.now.0 % INQUIRY_INTERVAL != 0 {
            return;
        }

        let mut rng = self.rng(STREAM_AGENT);
        let mut investigation = match self.investigation.take() {
            Some(i) => i,
            None => return,
        };

        // Resolution attempt: the volume is down to one region, so go there.
        if let Some(region) = investigation.volume.resolved() {
            let actually_there = self.entities[investigation.target.0 as usize].region == region;
            if actually_there {
                let alive = rng.chance(2, 3);
                let poster = self
                    .contracts
                    .iter()
                    .find(|c| c.id == investigation.contract)
                    .map(|c| c.poster);
                let archetype = self
                    .contracts
                    .iter()
                    .find(|c| c.id == investigation.contract)
                    .map(|c| c.archetype);
                self.commit(Intent::new(
                    region,
                    Event::ContractResolved {
                        contract: investigation.contract,
                        by: self.agent,
                        alive,
                    },
                ));
                // Delivering earns a favour from the poster's executive, which
                // becomes raw material for a later mission query.
                if let Some(poster) = poster {
                    if let Some(exec) = self.executive_of(poster) {
                        // What the client owes you follows from what you did for
                        // them. A bounty is settled in money; an audit buys their
                        // silence; a collection buys you a clean landing.
                        let class = match archetype {
                            Some(Archetype::Bounty) => ObligationClass::Debt,
                            Some(Archetype::Audit) => ObligationClass::Silence,
                            Some(Archetype::Collection) => ObligationClass::CleanLanding,
                            None => ObligationClass::Debt,
                        };
                        self.commit(Intent::new(
                            region,
                            Event::ObligationIncurred {
                                debtor: exec,
                                creditor: self.agent,
                                class,
                                weight: Fx::permille(800),
                                public: false,
                            },
                        ));
                    }
                }
                self.resolved_contracts += 1;
                self.investigation = None;
                return;
            }

            // The intelligence was stale. The volume widens to admit that.
            self.failed_resolutions += 1;
            let elsewhere = RegionId((region.0 + 1) % self.region_count);
            investigation.volume.constrain(elsewhere);
            self.investigation = Some(investigation);
            return;
        }

        // Otherwise: buy one more disclosure.
        let candidates: Vec<EntityId> = self
            .entities
            .iter()
            .filter(|e| e.id != self.agent && e.id != investigation.target)
            .filter(|e| !investigation.sources_used.contains(&e.id))
            .filter(|e| {
                self.beliefs
                    .belief_about(e.id, |p| {
                        matches!(p, Proposition::LocatedIn { who, .. } if *who == investigation.target)
                    })
                    .is_some()
            })
            .map(|e| e.id)
            .collect();

        // Prefer a source the agent already has leverage over. Favours exist
        // to be spent (§6.2) — an agent that picks sources at random redeems
        // almost nothing and the ledger becomes decorative.
        let owing: Vec<EntityId> = self
            .ledger
            .held_by(self.agent)
            .into_iter()
            .map(|o| o.debtor)
            .filter(|d| candidates.contains(d))
            .collect();

        let source = match rng.pick(if owing.is_empty() { &candidates } else { &owing }) {
            Some(s) => *s,
            None => {
                self.investigation = Some(investigation);
                return;
            }
        };

        let favour = self
            .ledger
            .held_by(self.agent)
            .into_iter()
            .find(|o| o.debtor == source);
        let incentive = match favour {
            Some(o) => Incentive::RedeemFavour(o.class),
            None if rng.chance(1, 6) => Incentive::Threat,
            None => Incentive::Money(rng.below(500) as u32),
        };

        let toward_asker = self.beliefs.disposition_toward(source, self.agent);
        let toward_target = self.beliefs.disposition_toward(source, investigation.target);
        let risk = if self.entities[source.0 as usize].employer.is_some() {
            Fx::permille(200)
        } else {
            Fx::permille(80)
        };

        investigation.sources_used.push(source);
        investigation.inquiries += 1;

        // The inquiry happens whether or not it succeeds. Asking is the event.
        let agent_region = self.entities[self.agent.0 as usize].region;
        self.commit(Intent::new(
            agent_region,
            Event::InquiryMade {
                asker: self.agent,
                source,
                about: investigation.target,
            },
        ));

        // …and the source now knows someone is looking, which is the closing
        // edge of the core loop. This belief propagates like any other.
        let source_region = self.entities[source.0 as usize].region;
        let poster = self
            .contracts
            .iter()
            .find(|c| c.id == investigation.contract)
            .map(|c| c.poster)
            .unwrap_or(CorpId(0));
        self.commit(Intent::new(
            source_region,
            Event::Witnessed {
                who: source,
                proposition: Proposition::BountyPosted {
                    on: investigation.target,
                    by: poster,
                },
            },
        ));

        // People warn their friends that someone has been asking after them.
        // This is §4's closing edge made concrete: the inquiry does not merely
        // enter the rumour mill and hope to arrive, it travels down the exact
        // relationship that made the source worth asking in the first place.
        if self.social[source.0 as usize].contains(&investigation.target) && rng.chance(1, 2) {
            self.commit(Intent::new(
                source_region,
                Event::Told {
                    from: source,
                    to: investigation.target,
                    proposition: Proposition::BountyPosted {
                        on: investigation.target,
                        by: poster,
                    },
                    confidence: Fx::permille(800),
                },
            ));
        }

        if will_disclose(toward_asker, toward_target, risk, incentive) {
            if let Incentive::RedeemFavour(class) = incentive {
                self.commit(Intent::new(
                    source_region,
                    Event::ObligationRedeemed {
                        debtor: source,
                        creditor: self.agent,
                        class,
                    },
                ));
            }
            let claim = self.beliefs.belief_about(source, |p| {
                matches!(p, Proposition::LocatedIn { who, .. } if *who == investigation.target)
            });
            if let Some(Proposition::LocatedIn { region, .. }) = claim.map(|b| b.proposition) {
                investigation.volume.constrain(region);
            }
        }

        self.investigation = Some(investigation);
    }

    /// A target who comes to believe there is a bounty on them runs. This is
    /// what makes information perishable: every inquiry shortens the window.
    fn step_flight(&mut self) {
        let mut rng = self.rng(STREAM_AGENT).derive(0xF11);
        let target = match &self.investigation {
            Some(i) => i.target,
            None => return,
        };

        let alarmed = self
            .beliefs
            .belief_about(target, |p| {
                matches!(p, Proposition::BountyPosted { on, .. } if *on == target)
            })
            .map(|b| b.confidence >= FLIGHT_THRESHOLD)
            .unwrap_or(false);

        if !alarmed || self.now.since(self.last_flight) < FLIGHT_COOLDOWN {
            return;
        }
        self.last_flight = self.now;

        let from = self.entities[target.0 as usize].region;
        let to = RegionId((from.0 + 1 + rng.below(self.region_count as u64 - 1) as u32) % self.region_count);
        self.commit(Intent::new(
            from,
            Event::TargetRelocated { who: target, from, to },
        ));

        // Whoever is standing in the new region sees them arrive. Beliefs about
        // the old location are now false and stay in circulation — which is the
        // whole point of §6.1.
        let arrivals: Vec<EntityId> = self
            .entities
            .iter()
            .filter(|e| e.region == to && e.id != target)
            .map(|e| e.id)
            .collect();
        for who in arrivals {
            self.commit(Intent::new(
                to,
                Event::Witnessed {
                    who,
                    proposition: Proposition::LocatedIn { who: target, region: to },
                },
            ));
        }
    }

    fn executive_of(&self, corp: CorpId) -> Option<EntityId> {
        self.entities
            .iter()
            .find(|e| e.employer == Some(corp) && e.role == Role::Executive)
            .map(|e| e.id)
    }

    // ---- reads ----------------------------------------------------------

    pub fn now(&self) -> Tick {
        self.now
    }

    pub fn seed(&self) -> u64 {
        self.seed
    }

    pub fn log(&self) -> &EventLog {
        &self.log
    }

    pub fn entities(&self) -> &[Entity] {
        &self.entities
    }

    pub fn entity(&self, id: EntityId) -> &Entity {
        &self.entities[id.0 as usize]
    }

    pub fn beliefs(&self) -> &BeliefStore {
        &self.beliefs
    }

    pub fn ledger(&self) -> &Ledger {
        &self.ledger
    }

    pub fn economy(&self) -> &Economy {
        &self.economy
    }

    pub fn contracts(&self) -> &[MissionContract] {
        &self.contracts
    }

    pub fn investigation(&self) -> Option<&Investigation> {
        self.investigation.as_ref()
    }

    pub fn region_name(&self, region: RegionId) -> &'static str {
        self.region_names[region.0 as usize]
    }

    pub fn region_count(&self) -> u32 {
        self.region_count
    }

    pub fn resolution_record(&self) -> (u32, u32) {
        (self.resolved_contracts, self.failed_resolutions)
    }

    /// Ground truth, for the §12 "% of beliefs that are false" metric. The sim
    /// knows; nobody inside it does.
    pub fn is_true(&self, proposition: &Proposition) -> bool {
        match *proposition {
            Proposition::LocatedIn { who, region } => {
                self.entities[who.0 as usize].region == region
            }
            Proposition::Solvent { corp } => !self.economy.corp(corp).cash.is_negative(),
            Proposition::Insolvent { corp } => self.economy.corp(corp).cash.is_negative(),
            Proposition::ControlsLane { corp, lane } => self.economy.lane_owner(lane) == corp,
            Proposition::Skimming { .. } => false,
            Proposition::BountyPosted { on, .. } => {
                self.contracts.iter().any(|c| c.target == on && !c.resolved)
            }
            Proposition::OwesObligation { debtor, creditor } => self
                .ledger
                .iter()
                .any(|o| o.debtor == debtor && o.creditor == creditor),
        }
    }

    /// A fingerprint of authoritative state. Two runs that agree here at every
    /// tick are bit-identical for replay purposes (§11 Tier 4).
    pub fn state_fingerprint(&self) -> u64 {
        let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
        let mut feed = |value: u64| {
            hash ^= value;
            hash = hash.wrapping_mul(0x1000_0000_01b3);
        };

        feed(self.now.0);
        feed(self.log.len() as u64);
        for entity in &self.entities {
            feed(entity.region.0 as u64);
        }
        for corp in self.economy.corps() {
            feed(corp.cash.to_raw() as u64);
        }
        for lane in 0..self.economy.lane_count() {
            feed(self.economy.lane_owner(LaneId(lane as u32)).0 as u64);
        }
        for (holder, belief) in self.beliefs.all() {
            feed(holder.0 as u64);
            feed(belief.confidence.to_raw() as u64);
            feed(belief.hops as u64);
        }
        feed(self.ledger.open_count() as u64);
        feed(self.contracts.len() as u64);
        hash
    }
}

/// Index of an entity in the world's entity table. Entities are stored dense
/// and in id order, so this is the identity — the function exists to make that
/// assumption explicit at the one call site that depends on it.
fn t_region(entities: &[Entity], id: EntityId) -> usize {
    debug_assert_eq!(entities[id.0 as usize].id, id, "entity table must be dense and id-ordered");
    id.0 as usize
}
