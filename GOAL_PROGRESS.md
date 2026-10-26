# Roadster simulation goal

**Latest checkpoint: 26 October 2026.** Read `Verification-20261026/CONTINUATION.md` first, then `Coordination/CODEX_RESUME_20261026.md` for takeover history. Current engine is UE5.8.4, HEAD `9e29e02` plus preserved changes. Updated Editor/Game builds pass and actual Simulator Lab checks23/23pass. Real-world collision checks found loop adhesion and high-speed penetration defects now under repair; headlight self-intersection candidate remains isolated until visual/clearance proof. Current evidence is under `Verification-20261026/`. Cooking, packaging, complete car-part clearance and target graphical quality remain unverified acceptance criteria.

User request dated 2026-10-25 (America/New_York). Project: `E:\TeslaRoadsterSim`.

## Objective and acceptance

Build a drivable Blender/Unreal Roadster experience with current 2026 reference-based exterior and cockpit, fan/pressure/thruster controls, editable loops/inverted/tunnel tracks, and high-quality real-time presentation. Preserve original inputs and existing local changes. Keep manufacturer claims, reference-derived measurements, simulation parameters, and verified behavior distinct.

Completion requires a usable Windows game, verified controls and track saving/loading, documented physics validation, actual gameplay captures and performance evidence, and a visual/reference audit. The requested exact proprietary fidelity and better-than-Forza quality are not established by a build or a screenshot; they remain open acceptance criteria. No background automation or external publishing is authorized or configured.

## Recovered checkpoint

- Existing Git HEAD: `f1cd5fe`, plus preexisting uncommitted Blender interior/wheel and cockpit interface edits.
- Existing source, assets and UI reference frames are preserved. Before-change patch and copies of dirty files: `Verification-20261025/BeforeChanges/`.
- Unreal Engine 5.7 and Blender 5.1 installed. NVIDIA RTX 5090 Laptop GPU, about 64 GB system RAM. E: about 657 GB free; C: about 2.3 GB free. Direct new temp/cache/output to E:.
- Supplied OBJ and CollegeRaimon GLB are being inspected read-only, with separate asset audit output.
- Existing README describes a packaged executable that is not present. Packaging and actual playability require verification.

## Work in progress

- Independent primary-source research audit, physics acceptance audit, and Blender asset inspection in bounded subtasks.
- Launch current Unreal game for visual and control inspection.
- Check shader/material usage warnings and runtime performance before packaging.

## Validation records

- Prior screenshot inspected: X-ray vehicle debug view and coarse mountain environment. Does not establish target visual quality.
- Existing standalone physics harnesses compile/run, but originally print results without acceptance assertions. New acceptance audit underway.

## Next checkpoints

1. Establish current playable baseline and capture exterior/cockpit/track builder.
2. Correct concrete runtime/visual defects, rebuild, and run meaningful checks.
3. Cook/package Windows game, verify packaged controls and saved tracks, record visual/performance limits.
4. Continue reference-based art/physics refinement toward user acceptance. Never call the overall goal complete while exactness/quality/verification criteria remain unfulfilled.
