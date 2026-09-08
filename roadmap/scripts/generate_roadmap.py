# -*- coding: utf-8 -*-
"""Builds public/roadmap.json from the authored task data.

Regenerating is **destructive to progress** unless `--preserve` is passed, which
merges status, notes and evidence from the existing file by task id. The default
is preserve; you have to ask for a wipe.

Ordering is strictly linear: every task depends on the one before it. That is a
deliberate simplification — a real dependency graph would let work proceed in
parallel, and there is one developer. A linear plan is honest about that and
makes "what is next" a single unambiguous answer, which is the whole point of a
tool an agent drives.
"""

import argparse
import io
import json
import os
import sys
from datetime import date, timedelta

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from roadmap_data import MILESTONES, M00, M01
from roadmap_data2 import M02, M03, M04
from roadmap_data3 import M05, M06, M07, M08
from roadmap_data4 import M09, M10, M11
from roadmap_data5 import M12, M13, M14
from roadmap_data6 import M15, M16, M17, M18
from roadmap_data7 import M19, M20, M21, M22, M23, M24

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(ROOT, "public", "roadmap.json")

# Week 0 is the Monday the Phase 0 work was declared finished.
PROJECT_START = date(2026, 9, 7)

GROUPS = [
    ("M00", M00), ("M01", M01), ("M02", M02), ("M03", M03), ("M04", M04),
    ("M05", M05), ("M06", M06), ("M07", M07), ("M08", M08), ("M09", M09),
    ("M10", M10), ("M11", M11), ("M12", M12), ("M13", M13), ("M14", M14),
    ("M15", M15), ("M16", M16), ("M17", M17), ("M18", M18), ("M19", M19),
    ("M20", M20), ("M21", M21), ("M22", M22), ("M23", M23), ("M24", M24),
]


def build():
    tasks = []
    index = 0
    previous_id = None

    for milestone_id, group in GROUPS:
        # Primary tasks first, then the supplementary ones, so each milestone
        # still reads as one narrative rather than as two interleaved lists.
        for entry in group:
            index += 1
            task_id = "T%03d" % index
            tasks.append(dict(
                id=task_id,
                milestone=milestone_id,
                order=index,
                title=entry["title"],
                detail=entry["detail"],
                acceptance=entry["acceptance"],
                estimateDays=entry["days"],
                designRefs=entry["refs"],
                dependsOn=[previous_id] if previous_id else [],
                # M0 is history: it is already built, and recording it as done is
                # what makes the burn-up chart start from the truth.
                status="done" if milestone_id == "M00" else "todo",
                startedAt=None,
                completedAt="2026-09-07" if milestone_id == "M00" else None,
                notes=[],
                evidence=[],
            ))
            previous_id = task_id

    calibrate(tasks)

    # Schedule. Five working days a week, one developer, no parallelism.
    day_cursor = 0.0
    for entry in tasks:
        entry["plannedStartDay"] = round(day_cursor, 2)
        day_cursor += entry["estimateDays"]
        entry["plannedEndDay"] = round(day_cursor, 2)
        entry["plannedStart"] = working_day_to_date(entry["plannedStartDay"]).isoformat()
        entry["plannedEnd"] = working_day_to_date(entry["plannedEndDay"]).isoformat()

    milestones = []
    for entry in MILESTONES:
        owned = [t for t in tasks if t["milestone"] == entry["id"]]
        milestones.append(dict(
            id=entry["id"],
            title=entry["title"],
            goal=entry["goal"],
            gate=entry["gate"],
            designRefs=entry["refs"],
            weekStart=entry["week_start"],
            weekEnd=entry["week_end"],
            taskCount=len(owned),
            estimateDays=sum(t["estimateDays"] for t in owned),
            plannedStart=owned[0]["plannedStart"] if owned else None,
            plannedEnd=owned[-1]["plannedEnd"] if owned else None,
            status="done" if entry["id"] == "M00" else "todo",
            gatePassedAt="2026-09-07" if entry["id"] == "M00" else None,
            evidence=[],
        ))

    return dict(
        project="LEDGER",
        subtitle="Star Citizen's technical model, GTA's climb, Hearts of Iron at the "
                 "corporate scale. Information is the scarce commodity and attention is "
                 "the one you cannot buy.",
        designDoc="docs/design/ledger-design.md",
        livingWorldDoc="docs/design/living-world.md",
        architectureDoc="docs/architecture/module-map.md",
        projectStart=PROJECT_START.isoformat(),
        generatedAt=date.today().isoformat(),
        totalTasks=len(tasks),
        totalEstimateDays=sum(t["estimateDays"] for t in tasks),
        milestones=milestones,
        tasks=tasks,
    )


def calibrate(tasks):
    """Scales each milestone's estimates to fit the weeks it was allotted.

    The task estimates and the milestone week ranges were authored separately
    and disagreed by a factor of two — the plan claimed fifty-eight weeks and
    the tasks summed to a hundred and eighteen. A schedule that contradicts
    itself is worse than no schedule, because it looks like a commitment.

    Rather than re-guessing two hundred and sixty estimates by hand, the
    *relative* weights are kept and the milestone total is scaled to its
    allotted span. The relative weights are the part that was actually reasoned
    about; the absolute ones were never more than a guess at velocity.
    """
    from roadmap_data import MILESTONES as SPANS
    spans = {m["id"]: (m["week_start"], m["week_end"]) for m in SPANS}

    for milestone_id, (week_start, week_end) in spans.items():
        owned = [t for t in tasks if t["milestone"] == milestone_id]
        if not owned:
            continue
        allotted = (week_end - week_start + 1) * 5.0
        current = sum(t["estimateDays"] for t in owned)
        if current <= 0:
            continue
        factor = allotted / current
        for entry in owned:
            # Half a day is the floor: below that it is not a task, it is a note.
            entry["estimateDays"] = max(0.5, round(entry["estimateDays"] * factor * 2) / 2)


def working_day_to_date(working_days):
    """Maps a working-day offset onto a calendar date, five days a week."""
    whole = int(working_days)
    weeks, remainder = divmod(whole, 5)
    return PROJECT_START + timedelta(days=weeks * 7 + remainder)


def merge_progress(fresh, existing):
    """Carries status, notes and evidence forward across a regeneration.

    Matched by task id. A task whose title changed keeps its progress, because
    the id is what the CLI and the evidence refer to; a task that disappears
    takes its progress with it, which is the correct behaviour for work that is
    no longer planned.
    """
    by_id = {t["id"]: t for t in existing.get("tasks", [])}
    carried = 0
    for entry in fresh["tasks"]:
        previous = by_id.get(entry["id"])
        if previous is None:
            continue
        for field in ("status", "startedAt", "completedAt", "notes", "evidence"):
            if field in previous:
                entry[field] = previous[field]
        carried += 1

    milestones_by_id = {m["id"]: m for m in existing.get("milestones", [])}
    for entry in fresh["milestones"]:
        previous = milestones_by_id.get(entry["id"])
        if previous is None:
            continue
        for field in ("status", "gatePassedAt", "evidence"):
            if field in previous:
                entry[field] = previous[field]

    return carried


def main():
    parser = argparse.ArgumentParser(description="Generate the LEDGER roadmap.")
    parser.add_argument("--wipe", action="store_true",
                        help="Discard existing progress instead of merging it forward.")
    arguments = parser.parse_args()

    fresh = build()

    if not arguments.wipe and os.path.exists(OUTPUT):
        with io.open(OUTPUT, encoding="utf-8") as handle:
            existing = json.load(handle)
        carried = merge_progress(fresh, existing)
        print("merged progress for %d existing tasks" % carried)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with io.open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(fresh, handle, indent=1, ensure_ascii=False)

    days = fresh["totalEstimateDays"]
    print("wrote %s" % OUTPUT)
    print("%d tasks across %d milestones" % (fresh["totalTasks"], len(fresh["milestones"])))
    print("%d working days planned = %.1f weeks = %.1f months" % (days, days / 5.0, days / 21.7))
    print("last task planned to finish %s" % fresh["tasks"][-1]["plannedEnd"])


if __name__ == "__main__":
    main()
