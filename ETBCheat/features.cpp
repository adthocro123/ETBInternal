#include "pch.h"
#include "features.h"
#include "Instances.hpp"

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

FString CharToFString(const char* str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len == 0) return FString();
    wchar_t* wstr = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
    FString fstr(wstr);
    delete[] wstr;
    return fstr;
}

ABPCharacter_Demo_C* Features::GetPawn()
{
    APawn* pawn = Instances::GetLocalPawn();
    if (pawn && pawn->IsA(ABPCharacter_Demo_C::StaticClass()))
        return static_cast<ABPCharacter_Demo_C*>(pawn);
    return nullptr;
}

void Features::OnPawnChange(ABPCharacter_Demo_C* newPawn)
{
    if (newPawn == g_lastKnownPawn) return;
    g_lastKnownPawn = newPawn;
    if (!newPawn) return;
    g_lastStaminaState   = !g_infiniteStaminaEnabled;
    g_originalSpeedSaved = false;
    g_lastSpeedHackState = !g_speedHackEnabled;
}

// Returns forward vector offset position in front of an actor
static FVector ForwardOffset(AActor* actor, float distance)
{
    FRotator rot = actor->K2_GetActorRotation();
    FVector fwd  = UKismetMathLibrary::GetForwardVector(rot);
    FVector loc  = actor->K2_GetActorLocation();
    return { loc.X + fwd.X * distance, loc.Y + fwd.Y * distance, loc.Z + fwd.Z * distance };
}

// -----------------------------------------------------------------------
// Spawn helpers
// -----------------------------------------------------------------------

static AActor* SpawnActorAt(UClass* ActorClass, AActor* WorldContext, FVector location)
{
    if (!ActorClass || !WorldContext) return nullptr;

    FTransform t{};
    t.Rotation    = { 0.f, 0.f, 0.f, 1.f };
    t.Translation = location;
    t.Scale3D     = { 1.f, 1.f, 1.f };

    AActor* spawned = UGameplayStatics::BeginSpawningActorFromClass(WorldContext, ActorClass, t, true, nullptr);
    if (spawned) UGameplayStatics::FinishSpawningActor(spawned, t);
    return spawned;
}

template<typename T>
void SpawnItemByType(ABPCharacter_Demo_C* PlayerCharacter, int count)
{
    if (!PlayerCharacter) return;

    UClass* ItemClass = T::StaticClass();
    if (!ItemClass) {
        std::cout << "[ItemSpawner] ERROR: StaticClass() null\n";
        return;
    }

    // Try live world instance first (most reliable FName source)
    FName ItemID;
    for (int i = UObject::GObjects->Num() - 1; i >= 0; --i)
    {
        UObject* obj = UObject::GObjects->GetByIndex(i);
        if (!obj || !obj->IsA(ItemClass)) continue;
        if (obj->GetFullName().find("Default__") != std::string::npos) continue;
        T* live = static_cast<T*>(obj);
        if (!live->ID.IsNone()) { ItemID = live->ID; break; }
    }

    // Fallback: CDO
    if (ItemID.IsNone())
    {
        T* cdo = static_cast<T*>(ItemClass->ClassDefaultObject);
        if (cdo) ItemID = cdo->ID;
    }

    std::cout << "[ItemSpawner] " << ItemClass->GetName()
              << " ID=" << ItemID.ToString()
              << " IsNone=" << ItemID.IsNone() << "\n";

    if (ItemID.IsNone()) {
        std::cout << "[ItemSpawner] ERROR: could not resolve ID\n";
        return;
    }

    for (int i = 0; i < count; ++i)
        PlayerCharacter->DropItem_SERVER(ItemID);
}

void Features::ItemSpawner(int selectedItemIndex, int spawnCount)
{
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;

    switch (selectedItemIndex)
    {
    case 0:  SpawnItemByType<ABP_Item_AlmondWater_C>(pc, spawnCount);    break;
    case 1:  SpawnItemByType<ABP_Juice_C>(pc, spawnCount);               break;
    case 2:  SpawnItemByType<ABP_EnergyBar_C>(pc, spawnCount);           break;
    case 3:  SpawnItemByType<ABP_Item_Flashlight_C>(pc, spawnCount);     break;
    case 4:  SpawnItemByType<ABP_Item_Chainsaw_C>(pc, spawnCount);       break;
    case 5:  SpawnItemByType<ABP_Item_BugSpray_C>(pc, spawnCount);       break;
    case 6:  SpawnItemByType<ABP_Liquid_Pain_C>(pc, spawnCount);         break;
    case 7:  SpawnItemByType<ABP_Item_Knife_C>(pc, spawnCount);          break;
    case 8:  SpawnItemByType<ABP_Item_Crowbar_C>(pc, spawnCount);        break;
    case 9:  SpawnItemByType<ABP_FlareGun_C>(pc, spawnCount);            break;
    case 10: SpawnItemByType<ABP_MothJelly_C>(pc, spawnCount);           break;
    case 11: SpawnItemByType<ABP_Rope_C>(pc, spawnCount);                break;
    case 12: SpawnItemByType<ABP_Item_AlmondBottle_C>(pc, spawnCount);   break;
    case 13: SpawnItemByType<ABP_Item_Firework_C>(pc, spawnCount);       break;
    case 14: SpawnItemByType<ABP_Item_Ticket_C>(pc, spawnCount);         break;
    case 15: SpawnItemByType<ABP_Diving_Helmet_C>(pc, spawnCount);       break;
    case 16: SpawnItemByType<ABP_Item_Camera_C>(pc, spawnCount);         break;
    case 17: SpawnItemByType<ABP_Item_Glowstick_Red_C>(pc, spawnCount);  break;
    case 18: SpawnItemByType<ABP_Item_Glowstick_Blue_C>(pc, spawnCount); break;
    case 19: SpawnItemByType<ABP_Item_Glowstick_Yellow_C>(pc, spawnCount); break;
    case 20: SpawnItemByType<ABP_Thermometer_C>(pc, spawnCount);         break;
    case 21: SpawnItemByType<ABP_WalkieTalkie_C>(pc, spawnCount);        break;
    case 22: SpawnItemByType<ABP_DroppedItem_LiDAR_C>(pc, spawnCount);   break;
    }
}

void Features::SpawnCreature(int selectedCreatureIndex)
{
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) { std::cout << "[CreatureSpawner] ERROR: no pawn\n"; return; }

    FVector spawnLoc = ForwardOffset(pc, 300.f);

    UClass* cls = nullptr;
    switch (selectedCreatureIndex)
    {
    case 0: cls = ABP_Hound_C::StaticClass();          break;
    case 1: cls = ABP_Moth_C::StaticClass();           break;
    case 2: cls = ABP_Roaming_Smiler_C::StaticClass(); break;
    case 3: cls = ABP_SkinStealer_C::StaticClass();    break;
    case 4: cls = ABacteria_BP_C::StaticClass();       break;
    case 5: cls = ABacteria_Roaming_BP_C::StaticClass(); break;
    case 6: cls = ABP_BoneThief_C::StaticClass();      break;
    }

    if (!cls) return;
    std::cout << "[CreatureSpawner] Spawning " << cls->GetName() << "\n";
    AActor* spawned = SpawnActorAt(cls, pc, spawnLoc);
    std::cout << (spawned ? "[CreatureSpawner] OK\n" : "[CreatureSpawner] FAILED\n");
}

// -----------------------------------------------------------------------
// Monster control
// -----------------------------------------------------------------------

void Features::KillAllMonsters()
{
    int count = 0;
    auto killClass = [&](UClass* cls) {
        if (!cls) return;
        for (int i = 0; i < UObject::GObjects->Num(); ++i)
        {
            UObject* obj = UObject::GObjects->GetByIndex(i);
            if (!obj || !obj->IsA(cls)) continue;
            if (obj->GetFullName().find("Default__") != std::string::npos) continue;
            static_cast<AActor*>(obj)->K2_DestroyActor();
            ++count;
        }
    };
    killClass(ABP_Hound_C::StaticClass());
    killClass(ABP_Moth_C::StaticClass());
    killClass(ABP_Roaming_Smiler_C::StaticClass());
    killClass(ABP_SkinStealer_C::StaticClass());
    killClass(ABacteria_BP_C::StaticClass());
    killClass(ABacteria_Roaming_BP_C::StaticClass());
    killClass(ABP_BoneThief_C::StaticClass());
    std::cout << "[KillMonsters] Destroyed " << count << " entities\n";
}

void Features::TeleportMonstersToMe()
{
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;
    FVector myLoc = pc->K2_GetActorLocation();
    int count = 0;

    auto teleportClass = [&](UClass* cls) {
        if (!cls) return;
        for (int i = 0; i < UObject::GObjects->Num(); ++i)
        {
            UObject* obj = UObject::GObjects->GetByIndex(i);
            if (!obj || !obj->IsA(cls)) continue;
            if (obj->GetFullName().find("Default__") != std::string::npos) continue;
            static_cast<AActor*>(obj)->K2_SetActorLocation(myLoc, false, nullptr, true);
            ++count;
        }
    };
    teleportClass(ABP_Hound_C::StaticClass());
    teleportClass(ABP_Moth_C::StaticClass());
    teleportClass(ABP_Roaming_Smiler_C::StaticClass());
    teleportClass(ABP_SkinStealer_C::StaticClass());
    teleportClass(ABacteria_BP_C::StaticClass());
    teleportClass(ABacteria_Roaming_BP_C::StaticClass());
    teleportClass(ABP_BoneThief_C::StaticClass());
    std::cout << "[TeleportMonsters] Moved " << count << " entities\n";
}

void Features::FreezeMonstersTick()
{
    if (!g_freezeMonstersEnabled) return;

    UClass* charClass = ACharacter::StaticClass();
    auto freezeClass = [&](UClass* cls) {
        if (!cls) return;
        for (int i = 0; i < UObject::GObjects->Num(); ++i)
        {
            UObject* obj = UObject::GObjects->GetByIndex(i);
            if (!obj || !obj->IsA(cls)) continue;
            if (obj->GetFullName().find("Default__") != std::string::npos) continue;
            // BoneThief is APawn, not ACharacter — skip if no CharacterMovement
            if (!obj->IsA(charClass)) continue;
            ACharacter* ch = static_cast<ACharacter*>(obj);
            if (ch->CharacterMovement)
            {
                ch->CharacterMovement->MaxWalkSpeed = 0.f;
                ch->CharacterMovement->Velocity = { 0.f, 0.f, 0.f };
                ch->CharacterMovement->StopMovementImmediately();
            }
        }
    };
    freezeClass(ABP_Hound_C::StaticClass());
    freezeClass(ABP_Moth_C::StaticClass());
    freezeClass(ABP_Roaming_Smiler_C::StaticClass());
    freezeClass(ABP_SkinStealer_C::StaticClass());
    freezeClass(ABacteria_BP_C::StaticClass());
    freezeClass(ABacteria_Roaming_BP_C::StaticClass());
}

void Features::NoAggroTick()
{
    if (g_noAggroEnabled == g_noAggroLastState) return;
    g_noAggroLastState = g_noAggroEnabled;

    bool shouldActivate = !g_noAggroEnabled;

    auto processClass = [&](UClass* cls) {
        if (!cls) return;
        for (int i = 0; i < UObject::GObjects->Num(); ++i)
        {
            UObject* obj = UObject::GObjects->GetByIndex(i);
            if (!obj || !obj->IsA(cls)) continue;
            if (obj->GetFullName().find("Default__") != std::string::npos) continue;
            AActor* actor = static_cast<AActor*>(obj);
            UActorComponent* comp = actor->GetComponentByClass(UFancyEntitySightingComponent::StaticClass());
            if (comp) comp->SetActive(shouldActivate, false);
        }
    };
    processClass(ABP_Hound_C::StaticClass());
    processClass(ABP_Moth_C::StaticClass());
    processClass(ABP_Roaming_Smiler_C::StaticClass());
    processClass(ABP_SkinStealer_C::StaticClass());
    processClass(ABacteria_BP_C::StaticClass());
    processClass(ABacteria_Roaming_BP_C::StaticClass());
    std::cout << "[NoAggro] Set to " << (g_noAggroEnabled ? "ON" : "OFF") << "\n";
}

// -----------------------------------------------------------------------
// SkinStealer Disguise
// -----------------------------------------------------------------------

void Features::SkinStealerDisguise()
{
    if (g_isSkinStealerDisguise) return;

    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) { std::cout << "[Disguise] No pawn\n"; return; }

    FVector spawnLoc = pc->K2_GetActorLocation();
    AActor* skin = SpawnActorAt(ABP_SkinStealer_C::StaticClass(), pc, spawnLoc);
    if (!skin) { std::cout << "[Disguise] Spawn failed\n"; return; }

    // Hide the real player body for everyone (bHidden is Net replicated)
    pc->SetActorHiddenInGame(true);

    // 1. Kill all collision — stops melee damage AND stops camera spring-arm
    //    sweep from compressing against the SkinStealer capsule
    skin->SetActorEnableCollision(false);

    // 2. Eject the AI controller so the behavior tree stops entirely
    if (skin->IsA(APawn::StaticClass()))
    {
        AController* ctrl = static_cast<APawn*>(skin)->GetController();
        if (ctrl) ctrl->UnPossess();
    }

    // 3. Belt + suspenders: disable sight component too
    UActorComponent* sight = skin->GetComponentByClass(UFancyEntitySightingComponent::StaticClass());
    if (sight) sight->SetActive(false, false);

    g_skinStealerActor     = skin;
    g_isSkinStealerDisguise = true;
    std::cout << "[Disguise] SkinStealer disguise activated\n";
}

void Features::Undisguise()
{
    if (!g_isSkinStealerDisguise) return;

    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (pc) pc->SetActorHiddenInGame(false);

    if (g_skinStealerActor)
    {
        g_skinStealerActor->K2_DestroyActor();
        g_skinStealerActor = nullptr;
    }

    g_isSkinStealerDisguise = false;
    std::cout << "[Disguise] Undisguised\n";
}

// Called from ReceiveTick — keeps the SkinStealer skin glued to player position/rotation
void Features::DisguiseTick(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!g_isSkinStealerDisguise || !g_skinStealerActor || !PlayerCharacter) return;

    FVector loc = PlayerCharacter->K2_GetActorLocation();
    FRotator rot = PlayerCharacter->K2_GetActorRotation();

    g_skinStealerActor->K2_SetActorLocation(loc, false, nullptr, false);
    g_skinStealerActor->K2_SetActorRotation(rot, false);

    // Mirror the player's velocity onto the SkinStealer's movement component so
    // its AnimBlueprint sees real speed and plays walk/run/idle correctly.
    // K2_SetActorLocation bypasses physics so velocity would otherwise stay at zero.
    if (PlayerCharacter->CharacterMovement && g_skinStealerActor->IsA(ACharacter::StaticClass()))
    {
        ACharacter* skinChar = static_cast<ACharacter*>(g_skinStealerActor);
        if (skinChar->CharacterMovement)
            skinChar->CharacterMovement->Velocity = PlayerCharacter->CharacterMovement->Velocity;
    }
}

// -----------------------------------------------------------------------
// Teleports
// -----------------------------------------------------------------------

void Features::TeleportToNearestItem()
{
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;

    FVector myLoc   = pc->K2_GetActorLocation();
    AActor* nearest = nullptr;
    float   minDist = FLT_MAX;

    UClass* itemClass = ABP_Item_C::StaticClass();
    if (!itemClass) return;

    for (int i = 0; i < UObject::GObjects->Num(); ++i)
    {
        UObject* obj = UObject::GObjects->GetByIndex(i);
        if (!obj || !obj->IsA(itemClass)) continue;
        if (obj->GetFullName().find("Default__") != std::string::npos) continue;
        AActor* actor = static_cast<AActor*>(obj);
        FVector loc = actor->K2_GetActorLocation();
        float dx = loc.X - myLoc.X, dy = loc.Y - myLoc.Y, dz = loc.Z - myLoc.Z;
        float dist = dx*dx + dy*dy + dz*dz;
        if (dist < minDist) { minDist = dist; nearest = actor; }
    }

    if (nearest)
    {
        FVector dest = nearest->K2_GetActorLocation();
        dest.Z += 90.f; // stand above it
        pc->K2_SetActorLocation(dest, false, nullptr, true);
        std::cout << "[TeleportItem] Teleported to nearest item\n";
    }
    else std::cout << "[TeleportItem] No items found\n";
}

void Features::TeleportToNearestMonster()
{
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;

    FVector myLoc   = pc->K2_GetActorLocation();
    AActor* nearest = nullptr;
    float   minDist = FLT_MAX;

    auto checkClass = [&](UClass* cls) {
        if (!cls) return;
        for (int i = 0; i < UObject::GObjects->Num(); ++i)
        {
            UObject* obj = UObject::GObjects->GetByIndex(i);
            if (!obj || !obj->IsA(cls)) continue;
            if (obj->GetFullName().find("Default__") != std::string::npos) continue;
            AActor* actor = static_cast<AActor*>(obj);
            FVector loc = actor->K2_GetActorLocation();
            float dx = loc.X - myLoc.X, dy = loc.Y - myLoc.Y, dz = loc.Z - myLoc.Z;
            float dist = dx*dx + dy*dy + dz*dz;
            if (dist < minDist) { minDist = dist; nearest = actor; }
        }
    };
    checkClass(ABP_Hound_C::StaticClass());
    checkClass(ABP_Moth_C::StaticClass());
    checkClass(ABP_Roaming_Smiler_C::StaticClass());
    checkClass(ABP_SkinStealer_C::StaticClass());
    checkClass(ABacteria_BP_C::StaticClass());
    checkClass(ABacteria_Roaming_BP_C::StaticClass());
    checkClass(ABP_BoneThief_C::StaticClass());

    if (nearest)
    {
        FVector dest = nearest->K2_GetActorLocation();
        dest.Z += 90.f;
        pc->K2_SetActorLocation(dest, false, nullptr, true);
        std::cout << "[TeleportMonster] Teleported to nearest entity\n";
    }
    else std::cout << "[TeleportMonster] No entities found\n";
}

void Features::SaveWaypoint(int slot)
{
    if (slot < 0 || slot >= 5) return;
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;
    g_waypoints[slot]    = pc->K2_GetActorLocation();
    g_waypointSaved[slot] = true;
    std::cout << "[Waypoint] Saved slot " << slot << "\n";
}

void Features::TeleportToWaypoint(int slot)
{
    if (slot < 0 || slot >= 5 || !g_waypointSaved[slot]) return;
    ABPCharacter_Demo_C* pc = Features::GetPawn();
    if (!pc) return;
    pc->K2_SetActorLocation(g_waypoints[slot], false, nullptr, true);
    std::cout << "[Waypoint] Teleported to slot " << slot << "\n";
}

// -----------------------------------------------------------------------
// Player actions
// -----------------------------------------------------------------------

void Features::ForceDropAllItems()
{
    APawn* localPawn = Instances::GetLocalPawn();
    auto allPlayers  = Instances::GetAllInstancesOf<ABPCharacter_Demo_C>();
    int count = 0;
    for (ABPCharacter_Demo_C* p : allPlayers)
    {
        if (!p || p == localPawn) continue;
        AMP_PS_C* ps = static_cast<AMP_PS_C*>(p->PlayerState);
        if (!ps) continue;
        // Drop each item in their replicated inventory
        for (int i = 0; i < ps->Items_Rep.Num(); ++i)
        {
            FName itemName = ps->Items_Rep[i];
            if (!itemName.IsNone())
            {
                p->DropItem_SERVER(itemName);
                ++count;
            }
        }
    }
    std::cout << "[ForceDropItems] Dropped " << count << " items\n";
}

void Features::KillAllPlayers()
{
    APawn* localPawn = Instances::GetLocalPawn();
    auto allPlayers  = Instances::GetAllInstancesOf<ABPCharacter_Demo_C>();
    for (ABPCharacter_Demo_C* p : allPlayers)
    {
        if (p && p != localPawn) p->LiquidPain();
    }
}

void Features::TeleportAllPlayersToMe()
{
    APawn* localPawn = Instances::GetLocalPawn();
    if (!localPawn) return;
    FVector myLoc   = localPawn->K2_GetActorLocation();
    auto allPlayers = Instances::GetAllInstancesOf<ABPCharacter_Demo_C>();
    for (ABPCharacter_Demo_C* p : allPlayers)
    {
        if (p && p != localPawn)
            p->K2_SetActorLocation(myLoc, false, nullptr, true);
    }
}

void Features::ForceEndLevel()
{
    UWorld* world = Instances::GetWorld();
    if (!world) return;
    AGameModeBase* gm = world->AuthorityGameMode;
    if (gm && gm->IsA(AMP_GameMode_C::StaticClass()))
        static_cast<AMP_GameMode_C*>(gm)->EndGame();
}

// -----------------------------------------------------------------------
// Tick-driven features
// -----------------------------------------------------------------------

void Features::GodModeTick(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!g_godModeEnabled || !PlayerCharacter) return;
    // Prevent death by resetting bIsDead and topping off sanity
    AFancyCharacter* fancy = static_cast<AFancyCharacter*>(PlayerCharacter);
    if (fancy->bIsDead)
    {
        fancy->bIsDead = false;
        std::cout << "[GodMode] Prevented death\n";
    }
    // Also keep sanity full
    AMP_PS_C* ps = static_cast<AMP_PS_C*>(PlayerCharacter->PlayerState);
    if (ps)
    {
        static ULONGLONG next = 0;
        ULONGLONG now = GetTickCount64();
        if (now >= next)
        {
            ps->SRV_AddSanity(100.f);
            ps->AddSanity(100.f);
            next = now + 2000;
        }
    }
}

void Features::InfiniteStamina(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!PlayerCharacter) return;
    if (g_infiniteStaminaEnabled != g_lastStaminaState)
    {
        PlayerCharacter->ShouldUseStamina = !g_infiniteStaminaEnabled;
        g_lastStaminaState = g_infiniteStaminaEnabled;
    }
}

void Features::InfiniteSanity(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!g_infiniteSanityEnabled || !PlayerCharacter) return;
    AMP_PS_C* ps = static_cast<AMP_PS_C*>(PlayerCharacter->PlayerState);
    if (!ps) return;
    static ULONGLONG next = 0;
    ULONGLONG now = GetTickCount64();
    if (now >= next) { ps->SRV_AddSanity(100.f); ps->AddSanity(100.f); next = now + 1000; }
}

void Features::SpeedChanger(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!PlayerCharacter || !PlayerCharacter->CharacterMovement) return;

    if (g_speedHackEnabled != g_lastSpeedHackState)
    {
        if (g_speedHackEnabled)
        {
            if (!g_originalSpeedSaved)
            {
                g_originalWalkSpeed   = PlayerCharacter->WalkSpeed;
                g_originalSprintSpeed = PlayerCharacter->SprintSpeed;
                g_originalSpeedSaved  = true;
            }
        }
        else
        {
            if (g_originalSpeedSaved)
            {
                PlayerCharacter->SetWalkSpeedServer(g_originalWalkSpeed);
                PlayerCharacter->SetSprintSpeedServer(g_originalSprintSpeed);
                PlayerCharacter->CharacterMovement->MaxWalkSpeed    = g_originalWalkSpeed;
                PlayerCharacter->CharacterMovement->MaxAcceleration = 2048.f;
                g_originalSpeedSaved = false;
            }
        }
        g_lastSpeedHackState = g_speedHackEnabled;
    }

    if (g_speedHackEnabled)
    {
        static ULONGLONG next = 0;
        ULONGLONG now = GetTickCount64();
        if (now >= next)
        {
            PlayerCharacter->SetWalkSpeedServer(g_speedValue);
            PlayerCharacter->SetSprintSpeedServer(g_speedValue);
            next = now + 100;
        }
        PlayerCharacter->CharacterMovement->MaxWalkSpeed    = g_speedValue;
        PlayerCharacter->CharacterMovement->MaxAcceleration = g_speedValue * 10.f;
    }
}

void Features::PlayerFly(ABPCharacter_Demo_C* PlayerCharacter)
{
    if (!PlayerCharacter) return;
    auto mv  = PlayerCharacter->CharacterMovement;
    auto cap = PlayerCharacter->CapsuleComponent;
    if (!mv || !cap) return;

    if (g_flyHackEnabled)
    {
        if (!g_wasFlying)
        {
            g_wasFlying = true;
            mv->MovementMode = EMovementMode::MOVE_Flying;
            cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            mv->GravityScale              = 0.f;
            mv->BrakingDecelerationFlying = 2000.f;
        }
        APlayerController* PC = Instances::GetPlayerController();
        if (!PC) return;
        FRotator camRot = PC->GetControlRotation();
        FVector dir     = { 0.f, 0.f, 0.f };
        if (GetAsyncKeyState('W') & 0x8000) dir += UKismetMathLibrary::GetForwardVector(camRot);
        if (GetAsyncKeyState('S') & 0x8000) dir -= UKismetMathLibrary::GetForwardVector(camRot);
        if (GetAsyncKeyState('D') & 0x8000) dir += UKismetMathLibrary::GetRightVector(camRot);
        if (GetAsyncKeyState('A') & 0x8000) dir -= UKismetMathLibrary::GetRightVector(camRot);
        if (GetAsyncKeyState(VK_SPACE)  & 0x8000) dir.Z += 1.f;
        if (GetAsyncKeyState(VK_LSHIFT) & 0x8000) dir.Z -= 1.f;
        if (!dir.IsZero())
            PlayerCharacter->LaunchCharacter(UKismetMathLibrary::Normal(dir, 0.0001f) * g_flySpeed, true, true);
        else
            mv->Velocity = { 0.f, 0.f, 0.f };
    }
    else
    {
        if (g_wasFlying)
        {
            g_wasFlying               = false;
            mv->MovementMode          = EMovementMode::MOVE_Walking;
            mv->Velocity              = { 0.f, 0.f, 0.f };
            cap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            mv->GravityScale              = 1.f;
            mv->BrakingDecelerationFlying = 0.f;
            mv->StopMovementImmediately();
        }
    }
}

void Features::ChatSpammer()
{
    if (!g_chatSpammerEnabled) return;
    static ULONGLONG next = 0;
    ULONGLONG now = GetTickCount64();
    if (now >= next)
    {
        UWB_Chat_C* chat = Instances::GetInstanceOf<UWB_Chat_C>();
        if (chat) chat->Send_Global_Message(CharToFString(g_chatSpamMessage));
        next = now + g_chatSpamDelay;
    }
}

void Features::UnlockLobbyLimit()
{
    if (!g_unlockPlayersEnabled) return;
    static ULONGLONG next = 0;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    auto widgets = Instances::GetAllInstancesOf<UW_CreateServer_C>();
    for (UW_CreateServer_C* w : widgets)
    {
        if (!w) continue;
        w->MaximumPlayers = 100;
        if (w->Slider_MaxPlayers)
        {
            w->Slider_MaxPlayers->MaxValue = 100.f;
            w->Slider_MaxPlayers->Value    = 100.f;
            w->ChangeMaxPlayerSlider(100);
        }
    }
    next = now + 800;
}

// -----------------------------------------------------------------------
// ESP
// -----------------------------------------------------------------------

struct ESPEntry { float x, y; const char* label; ImU32 color; };

static bool WorldToScreen(APlayerController* PC, const FVector& worldPos, float& outX, float& outY)
{
    FVector2D screen{};
    if (!UGameplayStatics::ProjectWorldToScreen(PC, worldPos, &screen, false)) return false;
    outX = screen.X;
    outY = screen.Y;
    return true;
}

static float DistSq(const FVector& a, const FVector& b)
{
    float dx = a.X-b.X, dy = a.Y-b.Y, dz = a.Z-b.Z;
    return dx*dx + dy*dy + dz*dz;
}

void Features::DrawESP(ImDrawList* drawList)
{
    if (!g_espEnabled) return;

    APlayerController* PC = Instances::GetPlayerController();
    if (!PC) return;

    APawn* localPawn = Instances::GetLocalPawn();
    FVector myLoc = localPawn ? localPawn->K2_GetActorLocation() : FVector{};

    const ImU32 colMonster = IM_COL32(255, 60,  60,  255);
    const ImU32 colItem    = IM_COL32(60,  255, 100, 255);
    const ImU32 colPlayer  = IM_COL32(60,  160, 255, 255);
    const float boxHalf    = 5.f;

    auto drawEntry = [&](AActor* actor, ImU32 col, const char* name) {
        if (!actor) return;
        FVector loc = actor->K2_GetActorLocation();
        float dist  = sqrtf(DistSq(loc, myLoc)) * 0.01f; // UU to meters approx
        float sx, sy;
        if (!WorldToScreen(PC, loc, sx, sy)) return;
        // Dot
        drawList->AddCircleFilled(ImVec2(sx, sy), boxHalf, col);
        // Label with distance
        char buf[128];
        snprintf(buf, sizeof(buf), "%s [%.0fm]", name, dist);
        drawList->AddText(ImVec2(sx + 7.f, sy - 6.f), col, buf);
    };

    // Monsters
    if (g_espMonsters)
    {
        struct MonsterDef { UClass* cls; const char* name; };
        MonsterDef defs[] = {
            { ABP_Hound_C::StaticClass(),           "Hound"       },
            { ABP_Moth_C::StaticClass(),             "Moth"        },
            { ABP_Roaming_Smiler_C::StaticClass(),   "Smiler"      },
            { ABP_SkinStealer_C::StaticClass(),      "SkinStealer" },
            { ABacteria_BP_C::StaticClass(),         "Bacteria"    },
            { ABacteria_Roaming_BP_C::StaticClass(),  "Bacteria(R)"},
            { ABP_BoneThief_C::StaticClass(),        "BoneThief"   },
        };
        for (auto& def : defs)
        {
            if (!def.cls) continue;
            for (int i = 0; i < UObject::GObjects->Num(); ++i)
            {
                UObject* obj = UObject::GObjects->GetByIndex(i);
                if (!obj || !obj->IsA(def.cls)) continue;
                if (obj->GetFullName().find("Default__") != std::string::npos) continue;
                drawEntry(static_cast<AActor*>(obj), colMonster, def.name);
            }
        }
    }

    // Items
    if (g_espItems)
    {
        UClass* itemCls = ABP_Item_C::StaticClass();
        if (itemCls)
        {
            for (int i = 0; i < UObject::GObjects->Num(); ++i)
            {
                UObject* obj = UObject::GObjects->GetByIndex(i);
                if (!obj || !obj->IsA(itemCls)) continue;
                if (obj->GetFullName().find("Default__") != std::string::npos) continue;
                ABP_Item_C* item = static_cast<ABP_Item_C*>(obj);
                // Store string to avoid dangling c_str() from temporary
                std::string idStr = item->ID.ToString();
                drawEntry(item, colItem, idStr.c_str());
            }
        }
    }

    // Players
    if (g_espPlayers)
    {
        auto players = Instances::GetAllInstancesOf<ABPCharacter_Demo_C>();
        for (ABPCharacter_Demo_C* p : players)
        {
            if (!p || p == localPawn) continue;
            drawEntry(p, colPlayer, "Player");
        }
    }
}
