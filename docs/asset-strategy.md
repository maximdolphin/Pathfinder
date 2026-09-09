# How content gets made

*The document somebody follows. ADR-0005 decides that everything is generated
or free; ADR-0006 decides that the rule runs once, offline, into a real asset.
This says what that means when you want to add a new kind of thing.*

## The three tiers

Everything in the game is in exactly one of these. The line is **whether the
thing can exist before the game runs.**

### 1. Baked

Produced by a generator run offline, saved as a `.uasset`, loaded at runtime
like any other asset.

Ships, buildings, props, rocks, vegetation, and **every material**.

They get Nanite, automatic LOD chains, mesh distance fields, GPU instancing,
streaming and cooking. None of that is work this project does; it comes with
the asset type, and none of it is available to a runtime mesh.

**Worked example — a new prop.**

1. Describe the geometry as a static function taking an `FLedgerMeshBuilder&`.
   Static, and separate from any actor, because the bake runs before anything
   exists. `ALedgerShip::DescribeHull` and `ALedgerSettlement::DescribeTree` are
   the two to copy.
2. If it has two colours, return the triangle index where the second begins;
   the bake turns that into a second material slot. Do not rely on vertex
   colour — see the trap below.
3. Call `LedgerMesh::Bake` from the `-bakemeshes` block in `LedgerWorld.cpp`,
   with `bNanite` set on its merits: on for dense meshes, off for anything that
   is a few dozen triangles.
4. Add it to `tools/verify_meshes.py`'s expectations.
5. Place it with a `UHierarchicalInstancedStaticMeshComponent` if there will be
   more than a handful.

**Worked example — a new material.** Write the graph as a `Build...` function in
`LedgerMaterial`, add a line to `BakeMaterials`, and add a `Create...` that
loads the baked asset with an editor-only fallback. The graph stays in C++
because a material that is code diffs, reviews and merges, and one that is a
binary asset does none of those.

### 2. Streamed

Generated at runtime, every frame it is needed, never saved.

**Planetary terrain, and nothing else.**

A 6,371 km sphere rendered from orbit down to 4.8 m quads is not a large export;
it is an impossible one. There is no authoring step that produces it and no disk
that holds it.

This is the one place where runtime generation is the right answer rather than
the default one. Everything else was runtime because nobody chose — that is what
ADR-0006 exists to correct, and adding to this tier needs the same argument:
*what would the offline artefact even be?*

**Worked example.** There isn't one. If you think something belongs here, write
down the size of the file it would otherwise be. If the number is not absurd, it
belongs in tier 1.

### 3. Placed

Actors that exist in the level, put there once, edited by hand.

The sun, the sky light, the post-process volume. Things that are configuration
rather than content, and that a person should be able to select and look at.

**Worked example — a new placed actor.** Add it to `tools/make_level.py` with
default settings, and configure it from `ULedgerWorldBuilder` using the
find-or-spawn helper. The level says *what exists*; the code says *how it
behaves*. Never both — a level that also carried the settings would be a second
place for them to drift.

## Rebuilding everything

```bash
python tools/generate_assets.py
```

Five steps, about a minute and a half: level, surfaces, meshes, provenance,
materials. Order matters once — materials come after surfaces, because the
terrain material samples the scans and reads their tiling out of the manifest.

Nothing is skipped on the grounds of being up to date. A dependency graph is
wrong in one direction or the other, and being wrong towards "skipped a step"
means shipping a stale asset.

**Steps are judged by their artefact, not their exit code.** Both Unreal entry
points return non-zero on a successful run: the import commandlet validates an
unused global block and fails on it, and the editor's shutdown does not reliably
return zero. A generator that trusts exit codes either fails on success or, much
worse, succeeds on failure.

## What is in version control

| | |
|---|---|
| Committed | `surfaces/manifest.json`, `client/Content/Maps/Ledger.umap`, every generator and every validator |
| Ignored | `client/Content/Materials`, `client/Content/Meshes`, `client/Content/Surfaces`, `surfaces/<set>/`, `build/` |

Derived things are not in version control. A clone still runs: the editor falls
back to building materials in memory and says loudly that it did. A packaged
build needs the bake first.

Unreal packages embed GUIDs, so `.uasset` files are **not byte-reproducible**.
Of 76 generated assets exactly one is stable across regenerations — the
`.umap` — which happens to be the only one committed, so nothing churns. Do not
write a check that compares `.uasset` bytes across runs; check settings read
back off the asset, and check the render.

## Every asset says which rule made it

`Ledger.Generator` metadata, verified across the whole tree by
`tools/verify_assets.py`. An asset with no generator behind it is one nobody can
rebuild, and it survives because deleting it feels risky.

One exception, and it is written down rather than waved through: **the level
cannot carry the tag.** Package metadata set before a map save is dropped,
metadata set afterwards is not persisted by `save_asset`, and an actor tag on
the world settings did not take either. Its provenance is kept in
`build/make-level.txt` and the validator checks that line instead.

## Traps, each of which cost an afternoon

**Vertex colours do not survive the trip.** A colour that renders correctly on a
`UProceduralMeshComponent` does not reach the material once the same geometry is
a baked static mesh on an instanced component. Three explanations were tried and
all three were wrong. Use a material slot per colour.

**A material needs `bUsedWithInstancedStaticMeshes`.** Without it Unreal
silently swaps in the default material on an instanced component. The symptom is
black geometry with the material apparently assigned.

**`BuildFromMeshDescriptions` creates no material slots.** You must add them.
Without one, `SetMaterial(0, ...)` succeeds, reports the material back when
asked, and draws nothing.

**Generators must be idempotent.** `new_level` refuses to overwrite and
`SavePackage` will not replace an existing file. Both made a generator that
worked exactly once, and the second failure was hidden by `SAVE_NoError` — which
also produced a false "byte-identical" result, because unwritten files compare
equal to themselves.

**Numbers taken straight after a re-bake are wrong.** The first load of a freshly
built asset builds its derived data. The settlement measured 38.9 ms, then 8.8,
then 3.0 on successive runs with nothing changed between them.

## Where the seams still are

- `CreateFlatMaterial` is not baked, so buildings and the ship hull have no
  material in a packaged build. It wants a material instance per colour.
- Only the hull and two trees are baked meshes. The settlement's buildings are
  still a runtime procedural mesh.
- Nanite is on for the hull and off for the trees, which is right on the merits
  but means the project has no dense mesh actually exercising it yet.
