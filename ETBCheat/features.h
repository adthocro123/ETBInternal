#pragma once
#include "pch.h"
#include "imgui/imgui.h"

namespace Features
{
    // --- Core helpers ---
    ABPCharacter_Demo_C* GetPawn();
    void OnPawnChange(ABPCharacter_Demo_C* newPawn);

    // --- Tick-driven (called from ReceiveTick / WB_Chat Tick) ---
    void ItemSpawner(int selectedItemIndex, int spawnCount);
    void SpeedChanger(ABPCharacter_Demo_C* PlayerCharacter);
    void InfiniteStamina(ABPCharacter_Demo_C* PlayerCharacter);
    void InfiniteSanity(ABPCharacter_Demo_C* PlayerCharacter);
    void PlayerFly(ABPCharacter_Demo_C* PlayerCharacter);
    void ChatSpammer();
    void GodModeTick(ABPCharacter_Demo_C* PlayerCharacter);
    void FreezeMonstersTick();
    void NoAggroTick();
    void UnlockLobbyLimit();

    // --- One-shot actions (must run on game thread via pending flags) ---
    void SpawnCreature(int selectedCreatureIndex);
    void KillAllMonsters();
    void TeleportMonstersToMe();
    void SkinStealerDisguise();
    void Undisguise();
    void DisguiseTick(ABPCharacter_Demo_C* PlayerCharacter);
    void TeleportToNearestItem();
    void TeleportToNearestMonster();
    void SaveWaypoint(int slot);
    void TeleportToWaypoint(int slot);
    void ForceDropAllItems();
    void KillAllPlayers();
    void TeleportAllPlayersToMe();
    void ForceEndLevel();

    // --- ESP (called from render thread / ImGui) ---
    void DrawESP(ImDrawList* drawList);

    // ---------------------------------------------------------------
    // State — items
    inline int  g_selectedItemIndex  = 0;
    inline int  g_spawnCount         = 1;

    // State — player movement
    inline bool  g_speedHackEnabled     = false;
    inline float g_speedValue           = 2500.f;
    inline float g_originalWalkSpeed    = 0.f;
    inline float g_originalSprintSpeed  = 0.f;
    inline bool  g_originalSpeedSaved   = false;
    inline bool  g_lastSpeedHackState   = false;
    inline bool  g_flyHackEnabled       = false;
    inline float g_flySpeed             = 2000.f;
    inline bool  g_wasFlying            = false;

    // State — self
    inline bool g_infiniteStaminaEnabled = false;
    inline bool g_lastStaminaState       = false;
    inline bool g_infiniteSanityEnabled  = false;
    inline bool g_godModeEnabled         = false;

    // State — monsters
    inline int  g_selectedCreatureIndex  = 0;
    inline bool g_freezeMonstersEnabled  = false;
    inline bool g_noAggroEnabled         = false;
    inline bool g_noAggroLastState       = false;

    // State — SkinStealer disguise
    inline bool   g_isSkinStealerDisguise  = false;
    inline AActor* g_skinStealerActor      = nullptr;

    // State — waypoints (5 slots)
    inline FVector g_waypoints[5]    = {};
    inline bool    g_waypointSaved[5] = {};

    // State — ESP
    inline bool g_espEnabled  = false;
    inline bool g_espMonsters = true;
    inline bool g_espItems    = false;
    inline bool g_espPlayers  = true;

    // State — misc
    inline bool g_unlockPlayersEnabled = false;
    inline bool g_chatSpammerEnabled   = false;
    inline char g_chatSpamMessage[128] = "https://github.com/ghere699/ETBInternal";
    inline int  g_chatSpamDelay        = 500;
    inline bool g_Unload               = false;

    // Internal (pawn tracking)
    inline ABPCharacter_Demo_C* g_lastKnownPawn = nullptr;

    // ---------------------------------------------------------------
    // Pending action flags — set from render thread, executed on game thread
    inline bool g_spawnItemPending          = false;
    inline bool g_spawnCreaturePending      = false;
    inline bool g_killMonstersPending       = false;
    inline bool g_teleportMonstersPending   = false;
    inline bool g_skinStealerDisguisePending = false;
    inline bool g_undisguisePending          = false;
    inline bool g_teleportNearestItemPending    = false;
    inline bool g_teleportNearestMonsterPending = false;
    inline int  g_saveWaypointPending       = -1;  // slot, -1 = none
    inline int  g_teleportWaypointPending   = -1;  // slot, -1 = none
    inline bool g_forceDropItemsPending     = false;
}
