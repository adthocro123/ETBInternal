# ETBInternal — Escape The Backrooms Internal Cheat

An internal DLL cheat for *Escape The Backrooms* (UE4 4.27 Shipping build), built on top of [ghere699's original source](https://github.com/ghere699/ETBInternal).  
Renders an ImGui overlay via a D3D12 Present hook and drives all game logic through a ProcessEvent hook.

> **⚠️ Disclaimer:** Educational purposes only. Using cheats in online games may result in a ban. The authors are not responsible for any consequences. **Use at your own risk.**

---

## Features

### Self
| Feature | Description |
|---|---|
| God Mode | Blocks death; keeps sanity at 100 |
| Infinite Stamina | Disables stamina consumption |
| Infinite Sanity | Continuously restores sanity |
| Speed Hack | Adjustable walk/sprint speed (slider) |
| Fly Mode | Free-fly with WASD + Space/Shift; adjustable speed |

### Items  *(Host only)*
| Feature | Description |
|---|---|
| Item Spawner | Spawn any of 23 items at any quantity |
| Teleport to Nearest Item | Warps you to the closest item in the level |

### Entities  *(Host only)*
| Feature | Description |
|---|---|
| Spawn Creature | Spawn any of 7 monsters in front of you |
| Kill All Monsters | Destroys every entity in the world |
| Teleport Monsters to Me | Pulls every entity to your position |
| Freeze All Monsters | Zeroes movement on all ACharacter monsters every tick |
| No Monster Aggro | Disables `UFancyEntitySightingComponent` on all entities |
| Teleport to Nearest Monster | Warps you to the closest entity |
| SkinStealer Disguise | Spawns a SkinStealer on top of you, hides your body (replicated), ejects its AI controller, and mirrors your movement animations |

### Players  *(Host only)*
| Feature | Description |
|---|---|
| Kill All Players | Calls `LiquidPain()` on every other player |
| Teleport All to Me | Moves every other player to your position |
| Force Drop All Items | Reads each player's `Items_Rep` inventory and calls `DropItem_SERVER` |
| Chat Spammer | Sends a configurable message on a configurable interval |

### World  *(Host only)*
| Feature | Description |
|---|---|
| Force End Level | Calls `AMP_GameMode_C::EndGame()` |
| Waypoints (5 slots) | Save your current position and warp back to it |

### ESP
| Feature | Description |
|---|---|
| Monster ESP | Red dots + name + distance for all 7 monster types |
| Item ESP | Green dots + item ID + distance |
| Player ESP | Blue dots + distance for other players |

---

## Prerequisites

| Tool | Purpose | Where to get |
|---|---|---|
| Visual Studio 2022 or 2026 | Compiler | https://visualstudio.microsoft.com |
| Windows SDK 10.0+ | DXGI / D3D headers | Installed with VS (Desktop C++ workload) |
| Dumper-7 | Generates the UE4 SDK from a running game | https://github.com/Encryqed/Dumper-7 |
| A DLL injector | Injects the compiled DLL | [Fate Client Injector](https://github.com/FateCheats/Fate-Client-Injector) or any manual-map injector |

---

## Step 1 — SDK

The `ETBCheat/SDK/` folder is **included in this repo** and matches the current game build. You can skip straight to Step 2 unless the game has updated since this repo was last committed.

**If the game has updated** (you get linker errors or crashes at startup), regenerate the SDK:

1. Launch *Escape The Backrooms*.
2. Run Dumper-7 (as Administrator) against `Backrooms-Win64-Shipping.exe`.
3. Wait for the dump to finish — it outputs a folder of `.hpp`/`.cpp` files.
4. Delete `ETBCheat/SDK/` and replace it with the new dump output.
5. Rebuild.

---

## Step 2 — Build

1. Clone this repo:
   ```
   git clone https://github.com/adthocro123/ETBInternal.git
   ```
2. Open `ETBCheat.sln` in Visual Studio.
3. Set configuration to **Release** / **x64**.
4. Build → Build Solution (`Ctrl+Shift+B`).
5. Output DLL: `x64/Release/ETBCheat.dll`

---

## Step 3 — Inject

1. Launch *Escape The Backrooms* and load into a level.
2. Open your DLL injector, target `Backrooms-Win64-Shipping.exe`, select `ETBCheat.dll`, and inject.
3. Watch stdout (e.g. via a console window or [DebugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview)) for confirmation messages:
   ```
   [+] ExecuteCommandLists (idx 10): 0x...
   [+] Present hook created.
   [+] ProcessEvent hook created successfully!
   [+] ImGui D3D12 Initialized!
   [+] -> Press F8 to toggle menu.
   ```
4. Press **`F8`** in-game to toggle the menu.

> **Note:** Inject *after* you are in-game (past the main menu), not on the title screen. ProcessEvent requires a live `UGameInstance`.

---

## Architecture

Understanding why the code is structured this way will help you extend it or port it to other UE4 games.

### Hook chain

```
Game launch
│
├── Hooks::Initialize()  (called from DllMain thread)
│   ├── Dummy D3D11 swap chain → captures vtable[8]  (Present)
│   │                                      vtable[13] (ResizeBuffers)
│   │                                      vtable[22] (Present1)
│   ├── Dummy D3D12 command queue → captures vtable[10] (ExecuteCommandLists)
│   └── UGameInstance vtable[0x44] → ProcessEvent
│
├── hkExecuteCommandLists  — captures the real ID3D12CommandQueue*
│
├── hkPresent / hkPresent1  [D3D12 render thread]
│   └── DoRender()
│       ├── One-time: init ImGui D3D12 backend
│       └── Per-frame: ImGui::NewFrame → GUI::Render() → ImGui::Render
│
└── hkProcessEvent  [game thread, fires on every UFunction call]
    └── on "ReceiveTick" (BPCharacter_Demo_C)
        ├── tick-driven features (SpeedChanger, GodMode, FreezeMonsters …)
        └── deferred one-shot actions (SpawnCreature, SkinStealerDisguise …)
```

### Render thread vs game thread — the critical rule

`GUI::Render()` runs on the D3D12 present thread. **Any call that touches game objects from this thread will crash** (race condition / no synchronisation with the game's physics/tick thread).

The solution used here: ImGui buttons set a `bool` **pending flag** instead of calling the SDK directly. On the next `ReceiveTick` (game thread), `hkProcessEvent` reads and clears the flag and runs the actual function.

```cpp
// WRONG — called from render thread:
if (ImGui::Button("Spawn Creature")) Features::SpawnCreature(...);

// CORRECT — deferred to game thread:
if (ImGui::Button("Spawn Creature")) Features::g_spawnCreaturePending = true;
// ... then in hkProcessEvent ReceiveTick:
if (Features::g_spawnCreaturePending) {
    Features::g_spawnCreaturePending = false;
    Features::SpawnCreature(Features::g_selectedCreatureIndex);
}
```

ESP drawing (`DrawESP`) is read-only and can safely run on the render thread.

### Key SDK types used

| Type | Purpose |
|---|---|
| `ABPCharacter_Demo_C` | Local player pawn |
| `AMP_PS_C` | Player state — holds `Items_Rep` inventory array |
| `AMP_GameMode_C` | Game mode — `EndGame()` |
| `UFancyEntitySightingComponent` | Monster sight/aggro component |
| `ABP_Item_C` | Base item class — `ID` (FName) |
| `UGameplayStatics::BeginSpawningActorFromClass` + `FinishSpawningActor` | Safe actor spawn |

### SkinStealer disguise — how it works

1. Spawn `ABP_SkinStealer_C` at the player's location.
2. Hide the real player body via `SetActorHiddenInGame(true)` — `bHidden` is Net-replicated, so other players also stop seeing the body.
3. Disable all collision on the SkinStealer (`SetActorEnableCollision(false)`) — prevents it from blocking the camera spring-arm and stops melee damage.
4. Eject the AI controller (`APawn::GetController()->UnPossess()`) — stops the behavior tree so it won't attack or wander.
5. Each `ReceiveTick`, `DisguiseTick()` sets the SkinStealer's location/rotation to match the player and mirrors the player's `CharacterMovement->Velocity` onto the skin — this makes the AnimBlueprint play walk/run/idle correctly since it reads velocity rather than actual movement.

### Item spawner — FName resolution

`DropItem_SERVER` needs the item's `FName ID`. The CDO (Class Default Object) often returns `None` in Shipping builds. The spawner first scans live GObjects for a world instance of the item class to get a valid ID; the CDO is only a fallback.

---

## Troubleshooting

**Menu never appears / no console output**
- Inject after you are fully loaded into a level, not on the title screen.
- Make sure you are injecting the x64 Release build, not Debug.

**`[- ] ProcessEvent hook failed`**
- `UGameInstance` could not be found — confirm the game is in an active level when you inject.

**Spawned creatures crash the game**
- Only trigger spawns from the game thread (the pending-flag pattern above). Calling `BeginSpawningActorFromClass` from the render thread is a guaranteed crash.

**SkinStealer kills you / camera bugs**
- Both are fixed in this version: collision is disabled on the spawned skin and the AI controller is ejected on spawn.

**SDK type not found after a game update**
- Regenerate the SDK with Dumper-7 against the new build.

---

## Credits

- [ghere699](https://github.com/ghere699/ETBInternal) — original project
- [ocornut/imgui](https://github.com/ocornut/imgui) — Dear ImGui
- [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook) — MinHook
- [Encryqed/Dumper-7](https://github.com/Encryqed/Dumper-7) — UE4 SDK generator

---

## License

MIT — see [LICENSE](LICENSE).
