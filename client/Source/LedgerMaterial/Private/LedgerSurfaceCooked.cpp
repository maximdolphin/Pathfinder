// Nothing left here.
//
// This file existed because shaders only compile at runtime in an editor
// build, so every material factory returned null in a packaged one and a
// cooked build had no look at all. Every material is now a baked asset
// loaded by LedgerMaterialLoad.cpp, in editor and packaged builds alike,
// and the last holdout -- the flat material, which used to bake a colour
// into a constant -- takes its colour as a parameter and is one asset with
// a dynamic instance per user.
//
// Kept as a marker rather than deleted, because "why is there no cooked
// path" is a reasonable question and this is where somebody looks.
