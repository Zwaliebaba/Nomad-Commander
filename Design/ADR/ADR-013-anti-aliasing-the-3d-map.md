# ADR-013 — Anti-aliasing the 3D map

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-027 (owner-visible)
**Cites:** GDD §13, §16; AGENTS.md R12, §5; ADR-009, ADR-012

## Context

**The question this ADR was written to answer has changed since the task that asked for it.** NC-027 was planned when R12 said the game drew straight into the back buffer with "no intermediate render target, no resolve pass and no present scale". Those three sentences ruled out MSAA, a post-pass and supersampling between them — every way of anti-aliasing there is — so the ADR was to be a list of what R12 would have to give up.

ADR-009 gave it up first, for other reasons. Every pass now draws into a 1920×1080 scene target which is *not* a back buffer, so DXGI's refusal to multisample a flip-model back buffer no longer binds, and a resolve has somewhere to resolve to. AGENTS.md §5 already says so: "**What is not decided is whether to pay for it.** NC-027 draws the map first, measures it aliased, and writes that ADR with a picture rather than an argument."

So this is a cost question, not a possibility question.

## Measurements

Taken from the running game at 1920×1080, reading the screen back as pixels. The map is three shaded spheres and two lanes; the desk sits in a side panel to the right of it.

| | |
|---|---|
| Map pixels painted (1-in-9 sample of the map's area) | 29,967 |
| Distinct colours in the map | **402** — the shading ramp across the spheres. A flat 2D disc is 1 to 3 |
| Silhouette rows measured | 655 |
| Distinct left-edge x values across those rows | 277 |
| Longest straight run at one x (a "tread" of the staircase) | 142 rows |
| **Edge pixels whose immediate outside neighbour is exactly the background** | **655 of 655** |

That last row is the whole finding, stated as a number: **there is no partial coverage anywhere on any silhouette.** Every edge pixel is fully sphere or fully background. The edges are not slightly jagged; they are maximally jagged, which is what "no anti-aliasing of any kind" looks like when you measure it rather than squint at it.

## Decision

**No anti-aliasing yet, and the trigger for adding it is named rather than left to taste.**

1. **The scene target and the depth target stay at `SampleDesc.Count` 1.** Nothing is resolved, and the frame is unchanged.
2. **MSAA on the scene target is the route when it is wanted**, and it is now a small change: create the scene target and `DepthTarget` multisampled, set `SampleDesc` on the mesh pipeline to match, and `ResolveSubresource` into a single-sampled target before ADR-009's present step scales it.
3. **The text survives that, which was the thing worth checking before recommending it.** §5 warns that filtering a glyph is blur, and the worry was that multisampling the target the glyphs are drawn into would antialias them too. It would not: a glyph is an axis-aligned quad on integer pixel boundaries, so every pixel it touches is fully covered and multisampling has nothing to blend; and its shape comes from `discard`, which kills a whole pixel rather than a sample. The 2D desk is unaffected either way.
4. **The trigger is motion.** Aliasing on a still image is a jagged edge, which this map has and which nobody has complained about. Aliasing on a *moving* image is crawl, which is what people actually notice. Nothing on this map moves yet — there is no camera controller (NC-027's out-of-scope list) and no animation. **The first task that moves the camera or a fleet along a lane is the one that should reopen this**, with the same measurement taken again while it moves.

## What this forecloses

**Nothing, and that is deliberate.** Every route stays open: MSAA as described above, a post-pass such as FXAA (which now has an intermediate target to read), or supersampling by making the scene target larger and letting the present step downsample. ADR-009 opened all three by accident and this ADR spends none of them.

What it does cost is a frame of someone's attention later: a decision deferred is a decision somebody has to come back to, and this one is deferred against a trigger rather than against a date, so it can be missed if nothing checks.

## Consequences

- `MeshPipeline` takes `PipelineDefaults::SampleDesc()`, which is `Count = 1`. Making the map multisampled means changing that *and* the two targets together — a pipeline's sample count must match its render target's, and the debug layer says so loudly, which is the safety net if somebody changes one and not the other.
- `DepthTarget` is created at one sample and its comment says why, pointing here.
- The 2D passes are untouched and have no depth bound, so none of this reaches the desk.

## The §13 guard, answered honestly

GDD §13 replaced "the 3D client waits" with a test: the 3D map earns its place when the player can say what it tells them that the 2D map did not. NC-027's own acceptance criterion insists the report answer it and says "'it looks better' is not an answer".

**The answer today is: nothing.** The map renders three shaded spheres and two lanes in perspective, depth-tested, and not one of those pixels carries information about the game. There are no systems on it, no fleets, no lanes with hours, no ownership colours that mean anything — NC-072 is what puts the universe on this map, and until it does, the 3D map tells the player exactly what an empty 2D map tells them.

Two things are *true* and are still not an answer to the guard: the spheres carry 402 distinct colours where a flat disc carries three, and things in front really do occlude things behind rather than being painter-ordered. Both are properties of the renderer, not of the game. **The 2D map stays**, as §13 and NC-027's out-of-scope list both require, and the comparison the guard asks for cannot be made until both are drawing the same universe.
