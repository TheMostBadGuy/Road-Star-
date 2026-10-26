Parked work in progress (moved out of Source/ so the module compiles).

RoadsterOverlays.h  - thermal / flow / physics-lab overlay components written by a parallel assistant
                      session on 2026-10-25 (~18:00). The header declares URoadsterFlowOverlay with a
                      TUniquePtr<FImpl> and an out-of-line destructor, but no .cpp defining any of the three
                      components existed, so UnrealHeaderTool's generated code failed (C4150) and the module
                      could not build. Nothing referenced these classes. Move the header back into
                      Source/RoadsterSim/Public together with an implementation to continue that work.
