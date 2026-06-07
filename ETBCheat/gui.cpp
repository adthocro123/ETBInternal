#include "pch.h"
#include "gui.h"
#include "features.h"
#include "imgui/imgui.h"

static const char* g_itemNames[] = {
    "Almond Water", "Juice", "Energy Bar", "Flashlight", "Chainsaw",
    "Bug Spray", "Liquid Pain", "Knife", "Crowbar", "Flare Gun",
    "Moth Jelly", "Rope", "Almond Bottle", "Firework", "Ticket",
    "Diving Helmet", "Camera", "Glowstick (Red)", "Glowstick (Blue)",
    "Glowstick (Yellow)", "Thermometer", "Walkie-Talkie", "Lidar"
};

static const char* g_creatureNames[] = {
    "Hound", "Moth", "Roaming Smiler", "Skin Stealer",
    "Bacteria", "Bacteria Roaming", "Bone Thief"
};

static const char* g_waypointLabels[] = { "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5" };

void GUI::Render()
{
    if (Features::g_espEnabled)
        Features::DrawESP(ImGui::GetBackgroundDrawList());

    ImGui::SetNextWindowSize(ImVec2(520, 540), ImGuiCond_Once);
    if (!ImGui::Begin("Escape The Backrooms Internal | F8 to toggle"))
    {
        ImGui::End();
        return;
    }

    ImGui::BeginTabBar("##tabs");

    // ===================================================================
    // SELF
    // ===================================================================
    if (ImGui::BeginTabItem("Self"))
    {
        ImGui::SeparatorText("Survival");
        ImGui::Checkbox("God Mode", &Features::g_godModeEnabled);
        ImGui::SameLine(); ImGui::TextDisabled("(blocks death + sanity drain)");
        ImGui::Checkbox("Infinite Stamina", &Features::g_infiniteStaminaEnabled);
        ImGui::Checkbox("Infinite Sanity",  &Features::g_infiniteSanityEnabled);

        ImGui::SeparatorText("Movement");
        ImGui::Checkbox("Speed Hack", &Features::g_speedHackEnabled);
        if (Features::g_speedHackEnabled)
            ImGui::SliderFloat("Speed##spd", &Features::g_speedValue, 100.f, 3000.f, "%.0f");
        ImGui::Checkbox("Fly", &Features::g_flyHackEnabled);
        if (Features::g_flyHackEnabled)
            ImGui::SliderFloat("Fly Speed##fly", &Features::g_flySpeed, 500.f, 10000.f, "%.0f");

        ImGui::EndTabItem();
    }

    // ===================================================================
    // ITEMS
    // ===================================================================
    if (ImGui::BeginTabItem("Items"))
    {
        ImGui::SeparatorText("Spawn  (Host Only)");
        ImGui::Combo("Item##item", &Features::g_selectedItemIndex, g_itemNames, IM_ARRAYSIZE(g_itemNames));
        ImGui::SliderInt("Amount##amt", &Features::g_spawnCount, 1, 50);
        if (ImGui::Button("Spawn Item"))
            Features::g_spawnItemPending = true;

        ImGui::SeparatorText("Teleport");
        if (ImGui::Button("Teleport to Nearest Item"))
            Features::g_teleportNearestItemPending = true;

        ImGui::EndTabItem();
    }

    // ===================================================================
    // ENTITIES
    // ===================================================================
    if (ImGui::BeginTabItem("Entities"))
    {
        ImGui::SeparatorText("Spawn  (Host Only)");
        ImGui::Combo("Creature##cr", &Features::g_selectedCreatureIndex, g_creatureNames, IM_ARRAYSIZE(g_creatureNames));
        if (ImGui::Button("Spawn Creature"))
            Features::g_spawnCreaturePending = true;

        ImGui::SeparatorText("Control  (Host Only)");
        if (ImGui::Button("Kill All Monsters"))
            Features::g_killMonstersPending = true;
        ImGui::SameLine();
        if (ImGui::Button("Teleport to Me"))
            Features::g_teleportMonstersPending = true;
        ImGui::Checkbox("Freeze All Monsters", &Features::g_freezeMonstersEnabled);
        ImGui::Checkbox("No Monster Aggro",    &Features::g_noAggroEnabled);
        if (ImGui::Button("Teleport to Nearest Monster"))
            Features::g_teleportNearestMonsterPending = true;

        ImGui::SeparatorText("SkinStealer Disguise  (Host Only)");
        if (!Features::g_isSkinStealerDisguise)
        {
            ImGui::TextDisabled("Replaces your body with a SkinStealer skin.");
            ImGui::TextDisabled("You still move normally — others see the monster.");
            if (ImGui::Button("Transform into SkinStealer  "))
                Features::g_skinStealerDisguisePending = true;
        }
        else
        {
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "  >> YOU ARE THE SKIN STEALER <<  ");
            if (ImGui::Button("Return to Human"))
                Features::g_undisguisePending = true;
        }

        ImGui::EndTabItem();
    }

    // ===================================================================
    // PLAYERS
    // ===================================================================
    if (ImGui::BeginTabItem("Players"))
    {
        ImGui::SeparatorText("Actions  (Host Only)");
        if (ImGui::Button("Kill All Players"))
            Features::KillAllPlayers();
        ImGui::SameLine();
        if (ImGui::Button("Teleport All to Me"))
            Features::TeleportAllPlayersToMe();
        if (ImGui::Button("Force Drop All Items"))
            Features::g_forceDropItemsPending = true;

        ImGui::SeparatorText("Chat Spammer");
        ImGui::Checkbox("Enable##cs", &Features::g_chatSpammerEnabled);
        if (Features::g_chatSpammerEnabled)
        {
            ImGui::InputText("Message##cmsg", Features::g_chatSpamMessage, IM_ARRAYSIZE(Features::g_chatSpamMessage));
            ImGui::SliderInt("Delay (ms)##cdel", &Features::g_chatSpamDelay, 100, 5000);
        }

        ImGui::EndTabItem();
    }

    // ===================================================================
    // WORLD
    // ===================================================================
    if (ImGui::BeginTabItem("World"))
    {
        ImGui::SeparatorText("Level  (Host Only)");
        if (ImGui::Button("Force End Level"))
            Features::ForceEndLevel();

        ImGui::SeparatorText("Waypoints");
        for (int i = 0; i < 5; ++i)
        {
            ImGui::PushID(i);
            if (ImGui::Button("Save"))
                Features::g_saveWaypointPending = i;
            ImGui::SameLine();
            if (Features::g_waypointSaved[i])
            {
                char lbl[64];
                snprintf(lbl, sizeof(lbl), "Go to %s  (%.0f, %.0f, %.0f)",
                    g_waypointLabels[i],
                    Features::g_waypoints[i].X,
                    Features::g_waypoints[i].Y,
                    Features::g_waypoints[i].Z);
                if (ImGui::Button(lbl))
                    Features::g_teleportWaypointPending = i;
            }
            else
            {
                ImGui::TextDisabled("%s  [empty]", g_waypointLabels[i]);
            }
            ImGui::PopID();
        }

        ImGui::EndTabItem();
    }

    // ===================================================================
    // ESP
    // ===================================================================
    if (ImGui::BeginTabItem("ESP"))
    {
        ImGui::Checkbox("Enable ESP", &Features::g_espEnabled);
        if (Features::g_espEnabled)
        {
            ImGui::Indent();
            ImGui::Checkbox("Monsters", &Features::g_espMonsters);
            ImGui::Checkbox("Items",    &Features::g_espItems);
            ImGui::Checkbox("Players",  &Features::g_espPlayers);
            ImGui::Unindent();
        }
        ImGui::EndTabItem();
    }

    // ===================================================================
    // MISC
    // ===================================================================
    if (ImGui::BeginTabItem("Misc"))
    {
        if (ImGui::Button("Unload Cheat"))
            Features::g_Unload = true;
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}
