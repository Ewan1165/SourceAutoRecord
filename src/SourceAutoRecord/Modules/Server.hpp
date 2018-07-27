#pragma once
#include "Client.hpp"
#include "Engine.hpp"

#include "Features/Routing.hpp"
#include "Features/Speedrun.hpp"
#include "Features/StepCounter.hpp"

#include "Cheats.hpp"
#include "Interfaces.hpp"
#include "Offsets.hpp"
#include "Utils.hpp"

#define IN_JUMP (1 << 1)

#ifdef _WIN32
#define AirMove_Mid_Offset 679
#define AirMove_Signature "F3 0F 10 50 40"
#define AirMove_Continue_Offset 5
#define AirMove_Skip_Offset 142
#endif

namespace Server {

VMT g_GameMovement;

using _UTIL_PlayerByIndex = void*(__func*)(int index);
_UTIL_PlayerByIndex UTIL_PlayerByIndex;

void* gpGlobals;

void* GetPlayer()
{
    return UTIL_PlayerByIndex(1);
}
int GetPortals()
{
    auto player = GetPlayer();
    return (player) ? *reinterpret_cast<int*>((uintptr_t)player + Offsets::iNumPortalsPlaced) : 0;
}

namespace Detour {
    bool JumpedLastTime = false;
    bool CallFromCheckJumpButton = false;
}

// CGameMovement::CheckJumpButton
DETOUR_T(bool, CheckJumpButton)
{
    auto mv = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::mv);
    auto m_nOldButtons = reinterpret_cast<int*>((uintptr_t)mv + Offsets::m_nOldButtons);

    auto enabled = (!Cheats::sv_bonus_challenge.GetBool() || Cheats::sv_cheats.GetBool()) && Cheats::sar_autojump.GetBool();
    auto stored = 0;

    if (enabled) {
        stored = *m_nOldButtons;

        if (!JumpedLastTime)
            *m_nOldButtons &= ~IN_JUMP;
    }

    JumpedLastTime = false;

    CallFromCheckJumpButton = true;
    auto result = Original::CheckJumpButton(thisptr);
    CallFromCheckJumpButton = false;

    if (enabled) {
        if (!(*m_nOldButtons & IN_JUMP))
            *m_nOldButtons = stored;
    }

    if (result) {
        JumpedLastTime = true;
        ++Stats::Jumps::Total;
        ++Stats::Steps::Total;
        Stats::Jumps::StartTrace(Client::GetAbsOrigin());
    }

    return result;
}

// CGameMovement::PlayerMove
DETOUR(PlayerMove)
{
    auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
    auto mv = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::mv);

    auto m_fFlags = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_fFlags);
    auto m_MoveType = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_MoveType);
    auto m_nWaterLevel = *reinterpret_cast<int*>((uintptr_t)player + Offsets::m_nWaterLevel);

    auto frametime = *reinterpret_cast<float*>((uintptr_t)gpGlobals + Offsets::frametime);
    auto m_vecVelocity = *reinterpret_cast<Vector*>((uintptr_t)mv + Offsets::m_vecVelocity2);

    // Landed after a jump
    if (Stats::Jumps::IsTracing
        && m_fFlags & FL_ONGROUND
        && m_MoveType != MOVETYPE_NOCLIP) {
        Stats::Jumps::EndTrace(Client::GetAbsOrigin(), Cheats::sar_stats_jumps_xy.GetBool());
    }

    StepCounter::ReduceTimer(frametime);

    // Player is on ground and moving etc.
    if (StepCounter::StepSoundTime <= 0
        && m_MoveType != MOVETYPE_NOCLIP
        && Cheats::sv_footsteps.GetFloat()
        && !(m_fFlags & (FL_FROZEN | FL_ATCONTROLS))
        && ((m_fFlags & FL_ONGROUND && m_vecVelocity.Length2D() > 0.0001f)
               || m_MoveType == MOVETYPE_LADDER)) {
        StepCounter::Increment(m_fFlags, m_MoveType, m_vecVelocity, m_nWaterLevel);
    }

    Stats::Velocity::Save(Client::GetLocalVelocity(), Cheats::sar_stats_velocity_peak_xy.GetBool());

    return Original::PlayerMove(thisptr);
}

// CGameMovement::FinishGravity
DETOUR(FinishGravity)
{
    if (CallFromCheckJumpButton && Cheats::sar_jumpboost.GetBool()) {
        auto player = *reinterpret_cast<void**>((uintptr_t)thisptr + Offsets::player);
        auto mv = *reinterpret_cast<CHLMoveData**>((uintptr_t)thisptr + Offsets::mv);

        auto m_bDucked = *reinterpret_cast<bool*>((uintptr_t)player + Offsets::m_bDucked);

        Vector vecForward;
        Math::AngleVectors(mv->m_vecViewAngles, &vecForward);
        vecForward.z = 0;
        Math::VectorNormalize(vecForward);

        float flSpeedBoostPerc = (!mv->m_bIsSprinting && !m_bDucked) ? 0.5f : 0.1f;
        float flSpeedAddition = fabs(mv->m_flForwardMove * flSpeedBoostPerc);
        float flMaxSpeed = mv->m_flMaxSpeed + (mv->m_flMaxSpeed * flSpeedBoostPerc);
        float flNewSpeed = (flSpeedAddition + mv->m_vecVelocity.Length2D());

        if (Cheats::sar_jumpboost.GetInt() == 1) {
            if (flNewSpeed > flMaxSpeed) {
                flSpeedAddition -= flNewSpeed - flMaxSpeed;
            }

            if (mv->m_flForwardMove < 0.0f) {
                flSpeedAddition *= -1.0f;
            }
        }

        Math::VectorAdd((vecForward * flSpeedAddition), mv->m_vecVelocity, mv->m_vecVelocity);
    }
    return Original::FinishGravity(thisptr);
}

// CGameMovement::AirMove
DETOUR_B(AirMove)
{
#ifdef _WIN32
    if (Cheats::sar_aircontrol.GetInt() >= 2) {
#else
    if (Cheats::sar_aircontrol.GetBool()) {
#endif
        return Original::AirMoveBase(thisptr);
    }
    return Original::AirMove(thisptr);
}

#ifdef _WIN32
// CGameMovement::AirMove
namespace Detour {
    uintptr_t AirMove_Skip;
    uintptr_t AirMove_Continue;
}
DETOUR_MID_MH(AirMove_Mid)
{
    __asm {
        pushad
        pushfd
    }

    if ((!Cheats::sv_bonus_challenge.GetBool() || Cheats::sv_cheats.GetBool()) && Cheats::sar_aircontrol.GetBool())
    {
        __asm {
            popfd
            popad
            jmp Detour::AirMove_Skip
        }
    }

    __asm {
        popfd
        popad
        movss xmm2, dword ptr[eax + 0x40]
        jmp Detour::AirMove_Continue
    }
}

// CBaseEntityOutput::FireOutput
DETOUR_MH(FireOutput, int a2, int a3, int a4, int a5, int a6, void* pActivator, void* pCaller, float fDelay)
{
    auto print = Cheats::sar_debug_entitiy_output.GetBool();

    if (pActivator) {
        auto m_iName = *reinterpret_cast<char**>((uintptr_t)pActivator + Offsets::m_iName);
        auto m_iClassname = *reinterpret_cast<char**>((uintptr_t)pActivator + Offsets::m_iClassname);

        Speedrun::timer->CheckRules(m_iName, Engine::tickcount);

        if (print) {
            console->Print("[%i] %s (%s) (activator)\n", Engine::GetSessionTick(), m_iName, m_iClassname);
        }
    }

    if (pCaller) {
        auto m_iName = *reinterpret_cast<char**>((uintptr_t)pCaller + Offsets::m_iName);
        auto m_iClassname = *reinterpret_cast<char**>((uintptr_t)pCaller + Offsets::m_iClassname);
        if (print) {
            console->Print("[%i] %s (%s) (caller)\n", Engine::GetSessionTick(), m_iName, m_iClassname);
        }
    }

    auto m_ActionList = *reinterpret_cast<CEventAction**>((uintptr_t)thisptr + Offsets::m_ActionList);
    auto ev = m_ActionList;
    while (ev) {
        if (print) {
            console->Msg("- [%i] %s -> %s (%s)\n", Engine::GetSessionTick(), ev->m_iTarget, ev->m_iTargetInput, ev->m_iParameter);
        }
        ev = ev->m_pNext;
    }

    return Trampoline::FireOutput(thisptr, a2, a3, a4, a5, a6, pActivator, pCaller, fDelay);
}
#endif

// CGameMovement::AirAccelerate
//DETOUR_B(AirAccelerate, Vector& wishdir, float wishspeed, float accel)
//{
//    //return Original::AirAccelerate(thisptr, wishdir, wishspeed, accel);
//    return Original::AirAccelerateBase(thisptr, wishdir, wishspeed, accel);
//}

void Hook()
{
    CREATE_VMT(Interfaces::IGameMovement, g_GameMovement)
    {
        HOOK(g_GameMovement, CheckJumpButton);
        HOOK(g_GameMovement, PlayerMove);

        if (Game::IsPortal2Engine()) {
            HOOK(g_GameMovement, FinishGravity);
            HOOK(g_GameMovement, AirMove);

            auto ctor = g_GameMovement->GetOriginalFunction<uintptr_t>(0);
            auto baseCtor = Memory::ReadAbsoluteAddress(ctor + Offsets::AirMove_Offset1);
            auto baseOffset = Memory::Deref<uintptr_t>(baseCtor + Offsets::AirMove_Offset2);
            Original::AirMoveBase = Memory::Deref<_AirMove>(baseOffset + Offsets::AirMove * sizeof(uintptr_t*));

#ifdef _WIN32
            auto airMoveMid = g_GameMovement->GetOriginalFunction<uintptr_t>(Offsets::AirMove) + AirMove_Mid_Offset;
            if (Memory::FindAddress(airMoveMid, airMoveMid + 5, AirMove_Signature) == airMoveMid) {
                MH_HOOK_MID(AirMove_Mid, airMoveMid);
                Detour::AirMove_Continue = airMoveMid + AirMove_Continue_Offset;
                Detour::AirMove_Skip = airMoveMid + AirMove_Skip_Offset;
                console->DevMsg("SAR: Verified sar_aircontrol 1!\n");
            } else {
                console->Warning("SAR: Failed to enable sar_aircontrol 1 style!\n");
            }
#endif

            /*HOOK(g_GameMovement, AirAccelerate);
            auto airAccelerateAddr = *reinterpret_cast<uintptr_t*>(baseOffset + Offsets::AirAccelerate * sizeof(uintptr_t*));
            Original::AirAccelerateBase = reinterpret_cast<_AirAccelerate>(airAccelerateAddr);*/
        }

        auto FullTossMove = g_GameMovement->GetOriginalFunction<uintptr_t>(Offsets::FullTossMove);
        gpGlobals = Memory::DerefDeref<void*>(FullTossMove + Offsets::gpGlobals);

#ifdef _WIN32
        if (Game::Version == Game::Portal2) {
            auto fio = SAR::Find("FireOutput");
            if (fio.Found) {
                MH_HOOK(FireOutput, fio.Address);
            }
        }
#endif
    }

    VMT g_ServerGameDLL;
    CREATE_VMT(Interfaces::IServerGameDLL, g_ServerGameDLL)
    {
        auto Think = g_ServerGameDLL->GetOriginalFunction<uintptr_t>(Offsets::Think);
        UTIL_PlayerByIndex = Memory::ReadAbsoluteAddress<_UTIL_PlayerByIndex>(Think + Offsets::UTIL_PlayerByIndex);
    }
}

void Unhook()
{
    UNHOOK(g_GameMovement, CheckJumpButton);
    UNHOOK(g_GameMovement, CheckJumpButton);
    UNHOOK(g_GameMovement, PlayerMove);
    UNHOOK(g_GameMovement, AirMove);
    //UNHOOK(g_GameMovement, AirAccelerate);
#ifdef _WIN32
    MH_UNHOOK(AirMove_Mid);
    MH_UNHOOK(FireOutput);
#endif
    DELETE_VMT(g_GameMovement);
}
}
