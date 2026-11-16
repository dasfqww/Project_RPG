#include "Development/RPGSoloRaidBot.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossPersistentHazard.h"
#include "AI/RPGBossSummonComponent.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Manager/TeamManager.h"

float RPGSoloRaidBot::Health(const AActor* Actor)
{
    const auto* Character = Cast<ARPGBaseCharacter>(Actor);
    const auto* ASC = IsValid(Character) ? Character->GetAbilitySystemComponent() : nullptr;
    const float Value = ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
    return FMath::IsFinite(Value) ? Value : 0.f;
}
ARPGBaseCharacter* RPGSoloRaidBot::SelectTarget(ARPGBossCharacter& Boss, ARPGPlayer& Player, bool bIgnoreMechanic)
{
    if (Health(&Boss) <= 0 || !Boss.BossAIComponent) return nullptr;
    if (bIgnoreMechanic) return &Boss;
    const auto State = Boss.BossAIComponent->GetBossState();
    const auto Objective = Boss.BossSummonObjectiveComponent->GetObjectiveState();
    const bool ObjectiveCurrent = Objective.bActive && Objective.EncounterId == State.EncounterId && Objective.PhaseIndex == State.PhaseIndex;
    auto* Teams = Boss.GetWorld()->GetSubsystem<UTeamManager>();
    ARPGBaseCharacter* Target = nullptr;
    int32 Slot = MAX_int32;
    for (const auto& Record : Boss.BossSummonComponent->GetSummons())
    {
        if (!Record.bInitialized || Record.EncounterId != State.EncounterId ||
            Record.PhaseIndex != State.PhaseIndex || !IsValid(Record.Pawn) || Record.Pawn->GetOwner() != &Boss ||
            Health(Record.Pawn) <= 0 || (ObjectiveCurrent && Record.Sequence != Objective.Sequence) ||
            !Teams || Teams->CompareTeams(&Player, Record.Pawn) != ERPGTeamComparison::DifferentTeams) continue;
        // Focus fire deterministically. Re-evaluate live records every tick, including retries.
        if (Record.Slot < Slot) { Slot = Record.Slot; Target = Record.Pawn; }
    }
    return Target ? Target : (ObjectiveCurrent ? nullptr : &Boss);
}
TArray<FRPGBossTelegraphGeometry> RPGSoloRaidBot::ReadDangers(ARPGBossCharacter& Boss)
{
    TArray<FRPGBossTelegraphGeometry> Dangers;
    const auto State = Boss.BossAIComponent->GetBossState();
    const auto Telegraph = Boss.BossAIComponent->GetTelegraphState();
    const auto* GS = Boss.GetWorld()->GetGameState();
    const double Now = GS ? GS->GetServerWorldTimeSeconds() : 0;
    if (Telegraph.bActive && Telegraph.bShowIndicator && Telegraph.EncounterId == State.EncounterId &&
        Telegraph.PhaseIndex == State.PhaseIndex && Telegraph.EndServerTime > Now && Telegraph.Geometry.bEnabled)
        Dangers.Add(Telegraph.Geometry);
    for (TActorIterator<ARPGBossPersistentHazard> It(Boss.GetWorld()); It; ++It)
    {
        const auto Hazard = It->GetHazardState();
        if (Hazard.SourcePawn != &Boss || Hazard.EncounterId != State.EncounterId || Hazard.EndServerTime <= Now ||
            !It->IsPresentationActive() || !It->IsIndicatorVisible()) continue;
        FRPGBossTelegraphGeometry Geometry;
        Geometry.bEnabled = true;
        Geometry.QueryTransform.SetLocation(Hazard.Center);
        Geometry.Shape.Type = ERPGHitQueryShape::Sphere;
        Geometry.Shape.Radius = Hazard.Radius;
        Dangers.Add(Geometry);
    }
    return Dangers;
}
bool RPGSoloRaidBot::TouchesDanger(const TArray<FRPGBossTelegraphGeometry>& Dangers, FVector Point, float Clearance)
{
    for (const auto& Danger : Dangers)
    {
        if (!Danger.bEnabled || !FRPGHitQueryMath::IsShapeValid(Danger.Shape)) continue;
        const auto Transform = FRPGHitQueryMath::ResolveShapeTransform(Danger.QueryTransform, Danger.Shape);
        // These authored indicators are ground footprints. Account for the player's capsule width.
        Point.Z = Transform.GetLocation().Z;
        if (FRPGHitQueryMath::IsPointInsideShape(Transform, Danger.Shape, Point)) return true;
        for (int32 Sample = 0; Sample < 12; ++Sample)
        {
            const float Angle = Sample * TWO_PI / 12;
            if (FRPGHitQueryMath::IsPointInsideShape(Transform, Danger.Shape,
                Point + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Clearance)) return true;
        }
    }
    return false;
}
bool RPGSoloRaidBot::FindEscape(const TArray<FRPGBossTelegraphGeometry>& Dangers, FVector Point, float Clearance, FVector& Goal)
{
    if (!TouchesDanger(Dangers, Point, Clearance)) return false;
    for (float Radius = 100; Radius <= 1000; Radius += 100)
        for (int32 Sample = 0; Sample < 16; ++Sample)
        {
            const float Angle = Sample * TWO_PI / 16;
            const FVector Candidate = Point + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius;
            if (!TouchesDanger(Dangers, Candidate, Clearance + 30)) { Goal = Candidate; return true; }
        }
    return false;
}
ARPGPlayer* RPGSoloRaidBot::FindLeader(UWorld& World, int32 PlayerId)
{
    if (PlayerId == INDEX_NONE) return nullptr;
    for (TActorIterator<ARPGPlayer> It(&World); It; ++It)
        if (const auto* PS = It->GetPlayerState(); PS && PS->GetPlayerId() == PlayerId) return *It;
    return nullptr;
}
