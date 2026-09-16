# Glossary — the GDD's terms in the code

One name per thing, fixed here before two tasks invent two. A row gives the GDD term, where the GDD defines it, the type that carries it, the file (AGENTS.md R7: one file per primary type), the project, and the task that creates it. Engine types are `namespace Neuron`; game types are `namespace Nomad` (R9). Add a row when a task introduces a term the GDD uses; renaming a row is an owner decision, because every later task builds on it.

## Engine (NeuronCore, NeuronClient, NeuronServer)

| Term | Type | File | Project | Task |
|---|---|---|---|---|
| Tick, the simulation's clock (GDD §7, R21) | `Tick` (`std::uint64_t`, one simulated minute) | `Tick.h` | NeuronCore | NC-010 |
| Typed index (AGENTS.md §2) | `Id<Tag>`; `using FleetId = Id<FleetTag>` and so on | `Id.h` | NeuronCore | NC-010 |
| The assert and the debug print | `NOMAD_ASSERT`, `NOMAD_VERIFY`, `DebugPrint` | `Debug.h` | NeuronCore | NC-001 |
| The Windows macro family and `<windows.h>` | (header only) | `NeuronCore.h` | NeuronCore | NC-001 |
| Pinned PRNG (R16) | `Random` | `Random.h` | NeuronCore | NC-011 |
| Fixed-point hundredths (R16, GDD §6 weights) | `Hundredths` | `Hundredths.h` | NeuronCore | NC-012 |
| Integer arithmetic the simulation can trust | `MulDivRound`, `SaturatingAdd`, ... | `IntegerMath.h` | NeuronCore | NC-012 |
| Byte writer and reader (the wire, the store) | `ByteWriter`, `ByteReader` | `ByteWriter.h`, `ByteReader.h` | NeuronCore | NC-013 |
| The simulation seam (bytes in, ticks, bytes out) | `Simulation` (abstract) | `Simulation.h` | NeuronCore | NC-014 |
| Wall time to ticks, with the compression rate | `TickSchedule` | `TickSchedule.h` | NeuronCore | NC-014 |
| Protocol envelope between client and host | `Protocol`, `MessageHeader`, `Channel` | `Protocol.h` | NeuronCore | NC-015 |
| Transport (abstract) and the in-process one | `Transport`, `MemoryTransport` | `Transport.h`, `MemoryTransport.h` | NeuronCore | NC-015 |
| The window, and the fixed screen size (R12) | `Window`, `SCREEN_WIDTH_PIXELS`, `SCREEN_HEIGHT_PIXELS` | `Window.h` | NeuronClient | NC-020 |
| The D3D12 device and queue | `GraphicsDevice` | `GraphicsDevice.h` | NeuronClient | NC-021 |
| The 1920×1080 target every pass draws into | `SceneTarget`, `TargetFault` | `SceneTarget.h` | NeuronClient | NC-021 |
| The swap chain, the back buffers and the frame's fencing | `SwapChainTarget` | `SwapChainTarget.h` | NeuronClient | NC-021 |
| The present scale: the scene target into the client area (ADR-009) | `PresentPass`, `PresentPass::Placement`, `PresentPass::Filter` | `PresentPass.h` | NeuronClient | NC-021 |
| Shared pipeline defaults (opaque by default; MSAA is unavailable on a flip-model back buffer) | `PipelineDefaults` | `PipelineDefaults.h` | NeuronClient | NC-022 |
| 2D primitives in pixel space | `PrimitiveBatch`, `PrimitiveVertex`, `Point`, `PrimitivePipeline` | `PrimitiveBatch.h`, `PrimitivePipeline.h` | NeuronClient | NC-006, NC-022 |
| The bitmap font, 96 glyphs, 8×8, one bit a pixel (R13) | `FONT_8X8_GLYPHS`, `FONT_FIRST_CODEPOINT`, `FONT_TOTAL_BYTES` | `BitmapFont.h` | NeuronClient | NC-023 |
| Text drawing | `TextRenderer`, `GLYPH_SCALE`, `GlyphVertex`, `TextExtent`, `GlyphPipeline` | `TextRenderer.h`, `GlyphPipeline.h` | NeuronClient | NC-023 |
| Embedded colours (R13, UI §2) | `Palette`, `Rgb()` | `Palette.h` | NeuronClient | NC-025 |
| The map's depth buffer (ADR-013) | `DepthTarget` | `DepthTarget.h` | NeuronClient | NC-027 |
| The map's camera, a plain value | `Camera` | `Camera.h` | NeuronClient | NC-027 |
| The map's geometry and its one pipeline | `MeshBuilder`, `MeshVertex`, `MeshSection`, `MeshPipeline` | `MeshBuilder.h`, `MeshPipeline.h` | NeuronClient | NC-027 |
| Input for one frame | `InputState`, `MouseButton`, `MousePoint` | `InputState.h` | NeuronClient | NC-024 |
| Immediate-mode UI (ADR-012) | `Ui`, `Rect`, `WidgetId`, `CELL_PIXELS` | `Ui.h`, `Rect.h` | NeuronClient | NC-025, NC-026 |
| Per-screen widget state (the caller's, not the Ui's) | `ScrollState`, `FieldState` | `UiState.h` | NeuronClient | NC-026 |
| The desk's icons, embedded like the font (R13) | `Icon`, `ICON_8X8_ART` | `IconAtlas.h` | NeuronClient | NC-026 |
| A session: one simulation driven on a schedule | `Session`, `SessionControlKind` | `Session.h` | NeuronServer | NC-030 |
| The universe store (R13's first exception; ADR-014: a seed and an input journal) | `UniverseStore` | `UniverseStore.h` | NeuronServer | NC-031 |
| The instrumentation log (R13's second exception, R24; ADR-015) | `InstrumentationLog`, `InstrumentationLog::Field` | `InstrumentationLog.h` | NeuronServer | NC-032 |
| The directory beside the executable | `ExecutableDirectory()` | `ExecutablePath.h` | NeuronCore | NC-031 |

## Game (GameLogic; the game half of NomadCommander)

| Term | GDD | Type | File | Task |
|---|---|---|---|---|
| Reality, the whole world state (R18) | §9 | `World` | `World.h` | NC-040 |
| The nomad as an entity type (R22): mothership, fleets, officers, outposts, record | §11, §14 | `Company`, `CompanyId` | `Company.h` | NC-040 |
| The mothership and its states | §5, §11 | `Mothership`, `MothershipState` | `Mothership.h` | NC-040 |
| Empire | §8 | `Empire`, `EmpireId` | `Empire.h` | NC-040 |
| Star system and its role | §7 | `StarSystem`, `SystemId`, `SystemRole` | `StarSystem.h` | NC-041 |
| Lane | §7 | `Lane`, `LaneId` | `Lane.h` | NC-041 |
| Universe generator | §7 | `UniverseGenerator` | `UniverseGenerator.h` | NC-041 |
| Ship class (four in v0.1) | §12 | `ShipClass`, `ShipCounts` | `ShipClass.h` | NC-040 |
| Fleet (counts per class, commander, history, veterancy) | §12 | `Fleet`, `FleetId`, `FleetPosition` | `Fleet.h` | NC-040 |
| Character: leader, admiral, officer | §8, §11 | `Character`, `CharacterId`, `CharacterRole` | `Character.h` | NC-040 |
| Good (four) and a system's market | §10 | `Good`, `Market`, `MarketState` | `Good.h`, `Market.h` | NC-045 |
| Convoy | §10 | a `Fleet` with `FleetRole::Convoy` and cargo | `Fleet.h` | NC-045 |
| Credits and the treasury | §5 | `Credits` | `Credits.h` | NC-040 |
| Upkeep, insolvency, mothballing | §5 | `Upkeep`, `MothballedHull` | `Upkeep.h` | NC-046 |
| Shipyard and the hull market | §5 | `Shipyard` | `Shipyard.h` | NC-046 |
| The fabricator (the floor) | §5 | `Fabricator` | `Fabricator.h` | NC-046 |
| Empire goal | §8 | `EmpireGoal`, `GoalKind` | `EmpireGoal.h` | NC-047 |
| War, truce, grudge | §7, §8 | `Relation`, `RelationState` | `Relation.h` | NC-047 |
| The tick resolver and its phase order | §2, §7 | `TickResolver` | `TickResolver.h` | NC-042 |
| Tuning values (R20) | §5, §6, §7, §10 | `Tuning` tables | `Tuning.h` | NC-042 |
| Consequence with its explanation (R19) | §9 | `Event`, `Explanation` | `Event.h`, `Explanation.h` | NC-042 |
| Player input | §3, §4 | `Input`, `InputKind` | `Input.h` | NC-042 |
| The game's `Simulation` | — | `NomadSimulation` | `NomadSimulation.h` | NC-042 |
| Wire schema the client may see (R18) | §4, §9 | `Wire*` records | `Wire*.h` | NC-042 onward |
| Report: source, age, reliability | §4 | `Report`, `ReportSource`, `SourceRecord` | `Report.h` | NC-050 |
| Sensor range and detection | §12 | `Sensor` | `Sensor.h` | NC-050 |
| Incident (a raid or attack an empire suffered) | §6 | `Incident`, `IncidentId` | `Incident.h` | NC-051 |
| Belief: one per empire about events | §6, §9 | `Belief`, `Suspicion` | `Belief.h` | NC-051 |
| Opinion: one per character about a company | §8, §9 | `Opinion` | `Opinion.h` | NC-051 |
| Institutional threat assessment and the overwrite rule | §9, §11 | `ThreatAssessment` | `ThreatAssessment.h` | NC-051 |
| Evidence and its weight | §6 | `Evidence`, `EvidenceKind` | `Evidence.h` | NC-052 |
| The inference rule and its thresholds | §6 | `Inference` | `Inference.h` | NC-052 |
| Accusation and the answers to it | §6 | `Accusation`, `AccusationAnswer` | `Accusation.h` | NC-052, NC-054 |
| Courier | §4, §9 | `Courier`, `CourierId`, `CourierPayload` | `Courier.h` | NC-053 |
| Covert raid by an empire | §6 | `CovertRaid` | `CovertRaid.h` | NC-055 |
| Marked goods, the loot trail, fencing | §5 | `CargoMark` | `Cargo.h` | NC-055 |
| Contract, offer, payout by attribution | §4, §8 | `Contract`, `ContractKind`, `ContractOffer` | `Contract.h` | NC-056 |
| Admiral traits and desperation | §8 | `AdmiralTraits`, `Desperation` | `Admiral.h` | NC-060 |
| The eight templates | §8 | `BattleTemplate` | `BattleTemplate.h` | NC-060 |
| Template selection from belief | §8 | `TemplateSelection`, `BelievedSituation` | `TemplateSelection.h` | NC-060 |
| Plan: base rules, overrides, branch budget | §4 | `Plan`, `BaseRules`, `Override`, `Trigger` | `Plan.h` | NC-061 |
| Command capacity (an officer's) | §11 | `Character::commandCapacity` | `Character.h` | NC-061 |
| Battle and its record (the replay) | §4, §8 | `Battle`, `BattleRecord`, `BattleRound` | `Battle.h`, `BattleRecord.h` | NC-062 |
| Hypothesis as selection; a reading | §4 | `Hypothesis`, `Reading` | `Hypothesis.h` | NC-063 |
| Operation and its projection | §3, §4 | `Operation`, `Projection` | `Operation.h` | NC-064 |
| The receipt | §4 | `Receipt` | `Receipt.h`, `ReceiptText.h` | NC-064 |
| Officer market, recruitment, leaving | §11 | `OfficerMarket` | `OfficerMarket.h` | NC-065 |
| Outpost, claim, governor policy | §11 | `Outpost`, `Claim`, `GovernorPolicy` | `Outpost.h` | NC-066 |
| Reinforcement timer and the active window | §7 | `ReinforcementTimer`, `ActiveWindow` | `Outpost.h`, `Company.h` | NC-066 |
| Situation board item | §3 | `BoardItem`, `BoardItemKind` | `BoardItem.h` | NC-067 |
| Intelligence purchase | §2, §4 | `IntelligenceOffer` | `BoardItem.h` | NC-067 |
| The scripted scenario | §15 | `Scenario`, `KESSEL_SCENARIO` | `Scenario.h`, `KesselScenario.h` | NC-090 |
| Instrumentation event kinds (R24) | §15 | `LogEvent` names | `LogEvent.h` | NC-043 |

## Client (NomadCommander, the game half)

| Term | GDD | Type | File | Task |
|---|---|---|---|---|
| The composition root: the one file that sees both halves | — | `App` | `App.h`, `App.cpp`, `Main.cpp` | NC-070 |
| The client's model, built only from wire messages (R18) | §4, §9 | `ClientModel` | `ClientModel.h` | NC-071 |
| The map, 3D inside a 2D desk | §13 | `MapScreen` | `MapScreen.h` | NC-072 |
| The situation board | §3 | `BoardScreen` | `BoardScreen.h` | NC-073 |
| Report, dossier and projection panels | §3, §4 | `ReportPanel`, `DossierPanel` | `ReportPanel.h`, `DossierPanel.h` | NC-074 |
| The accusation panel | §3, §9 | `AccusationPanel` | `AccusationPanel.h` | NC-075 |
| Hypothesis, operation composer, plan editor | §3, §4 | `OperationComposer`, `PlanEditor` | `OperationComposer.h`, `PlanEditor.h` | NC-076 |
| Operations in flight | §3 | `OperationPanel` | `OperationPanel.h` | NC-077 |
| The receipt and the replay | §4 | `ReceiptScreen`, `ReplayView` | `ReceiptScreen.h`, `ReplayView.h` | NC-078 |
| Market, shipyard, officers, outposts, contracts | §5, §8, §11 | `MarketScreen`, `ShipyardScreen`, `OfficerScreen`, `OutpostScreen`, `ContractScreen` | one file each | NC-079 |

## Names the GDD uses that the code does not

- **Nomad** (the entity) is `Company`, because `namespace Nomad` already exists and a type of the same name inside it would shadow the namespace for every qualified name written in game code. The GDD's "nomad" and the code's "company" are one thing; say so in a comment where it helps.
- **The player** is never a type (R22). Anything the GDD attributes to "the player" hangs off a `CompanyId`.
- **The truth** is `World`. Nothing outside `GameLogic` holds one, and inside it only the resolver and the battle mutate one (R18).
