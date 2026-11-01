#include "Tests/RPGSkillFinalCapNetworkProbe.h"
#include "Tests/RPGSkillDamageNetworkProbe.h"

#if !UE_BUILD_SHIPPING
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr float FixtureCap = 625.0f;
const FVector RejectedOffset(300, -120, 0);
bool Enabled()
{
    return FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RPGSkillDamageE2E")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RPGSkillFinalCapE2E"));
}
uint64 NetId(const UObject* Object)
{
    const UNetDriver* Driver = Object ? Object->GetWorld()->GetNetDriver() : nullptr;
    const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
    const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
    return Bridge ? Bridge->GetReplicatedRefHandle(Object).GetId() : 0;
}
}
#endif

FRPGSkillFinalCapNetworkProbe::~FRPGSkillFinalCapNetworkProbe()
{
#if !UE_BUILD_SHIPPING
    if (auto* Target = RejectedTarget.Get())
    {
        Target->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle);
        Target->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).Remove(HealthHandle);
    }
    if (auto* Skill = SourceSkill.Get(); Skill && !Skill->IsActive() && OriginalDefinition.IsValid())
        if (const auto* Property = FindFProperty<FObjectPropertyBase>(Skill->GetClass(), TEXT("SkillDefinition")))
            Property->SetObjectPropertyValue_InContainer(Skill, OriginalDefinition.Get());
#endif
}

void FRPGSkillFinalCapNetworkProbe::Fail(const TCHAR* Reason)
{
#if !UE_BUILD_SHIPPING
    if (!FailureReason.IsEmpty()) return;
    FailureReason = Reason;
    UE_LOG(LogTemp, Error, TEXT("RPG_SKILL_FINALCAP FAILED Reason=%s"), Reason);
#endif
}

bool FRPGSkillFinalCapNetworkProbe::PrepareProfile(URPGGameplayAbility_SkillContainer& Skill,
    URPGAbilitySystemComponent& ASC, const bool bServer)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled()) return false;
    if (bProfilePrepared) return true;
    if (Skill.IsActive() || Skill.HasAnyFlags(RF_ClassDefaultObject)) { Fail(TEXT("ProfileMustPrecedeInstanceActivation")); return false; }
    const auto* Property = FindFProperty<FObjectPropertyBase>(Skill.GetClass(), TEXT("SkillDefinition"));
    auto* Definition = Property ? Cast<URPGSkillDefinition>(Property->GetObjectPropertyValue_InContainer(&Skill)) : nullptr;
    if (!Definition || Definition->SecurityProfile.MaximumFinalDamagePerHit <= 1250.0f || NetId(&ASC) == 0) return false;
    auto* Copy = DuplicateObject<URPGSkillDefinition>(Definition, &Skill);
    if (!Copy) { Fail(TEXT("TransientDefinitionCopyFailed")); return false; }
    Copy->SetFlags(RF_Transient);
    OriginalCap = Definition->SecurityProfile.MaximumFinalDamagePerHit;
    OriginalDefinition = Definition;
    Copy->SecurityProfile.MaximumFinalDamagePerHit = FixtureCap;
    Property->SetObjectPropertyValue_InContainer(&Skill, Copy);
    SourceSkill = &Skill;
    bProfilePrepared = true;
    UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP PROFILE Role=%s SourceASCNetId=%llu Cap=625.000000 OriginalCap=%.6f Transient=1"),
        bServer ? TEXT("Server") : TEXT("Client"), NetId(&ASC), OriginalCap);
    return true;
#else
    return false;
#endif
}

void FRPGSkillFinalCapNetworkProbe::Tick(APlayerController& Controller, URPGAbilitySystemComponent& ASC,
    const FGameplayAbilitySpecHandle Handle)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled() || bPassed || !FailureReason.IsEmpty()) return;
    SourceASC = &ASC;
    auto* Spec = ASC.FindAbilitySpecFromHandle(Handle);
    auto* Skill = Spec ? Cast<URPGGameplayAbility_SkillContainer>(Spec->GetPrimaryInstance()) : nullptr;
    if (!Skill || Skill->GetClass()->GetName() != TEXT("GA_Player_ChargeSlash_C")) return;
    for (TActorIterator<ARPGSkillDamageNetworkTarget> It(Controller.GetWorld()); It; ++It)
    {
        if (It->TestIndex == 0) AcceptedTarget = *It;
        if (It->TestIndex == 4 && !Controller.HasAuthority())
        {
            if (RejectedTarget.IsValid() && RejectedTarget.Get() != *It) { Fail(TEXT("DuplicateRejectedTarget")); return; }
            RejectedTarget = *It;
        }
    }
    if (!AcceptedTarget.IsValid()) return;
    if (Controller.HasAuthority())
    {
        if (!PrepareProfile(*Skill, ASC, true)) return;
        if (!RejectedTarget.IsValid())
        {
            const auto* Team = Cast<IGenericTeamAgentInterface>(&Controller);
            const uint8 SourceTeam = Team ? Team->GetGenericTeamId().GetId() : 255;
            if (SourceTeam == 255 || !Controller.GetPawn()) { Fail(TEXT("MissingFixtureSourceTeam")); return; }
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Target = Controller.GetWorld()->SpawnActor<ARPGSkillDamageNetworkTarget>(
                Controller.GetPawn()->GetActorTransform().TransformPosition(RejectedOffset), FRotator::ZeroRotator, Params);
            auto* AI = Controller.GetWorld()->SpawnActor<AAIController>();
            if (!Target || !AI) { Fail(TEXT("RejectedTargetSpawnFailed")); return; }
            AI->SetGenericTeamId(FGenericTeamId(SourceTeam == 0 ? 1 : 0));
            AI->Possess(Target);
            Target->TestIndex = 4;
            Target->SourceASCNetId = NetId(&ASC);
            Target->Attributes()->SetDefense(0.5f);
            Target->bInputReady = true;
            Target->ForceNetUpdate();
            RejectedTarget = Target;
            HealthHandle = Target->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddLambda(
                [this](const FOnAttributeChangeData& Change)
                {
                    if (Change.NewValue != Change.OldValue) { ++HealthChanges; Fail(TEXT("RejectedServerHPChanged")); }
                });
            EffectHandle = Target->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
                [this, SourceTeam](UAbilitySystemComponent* ApplyingASC, const FGameplayEffectSpec& Effect, FActiveGameplayEffectHandle)
                {
                    auto* T = RejectedTarget.Get();
                    const auto* SkillInstance = SourceSkill.Get();
                    const auto* TargetTeam = T ? Cast<IGenericTeamAgentInterface>(T->GetController()) : nullptr;
                    const float Damage = Effect.GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false, -1);
                    if (++EffectCallbacks != 1 || ApplyingASC != SourceASC.Get() || !T || !SkillInstance ||
                        !Effect.Def || Effect.Def->GetClass()->GetName() != TEXT("GE_Shared_ApplyDamage_C") || Damage != 250.0f ||
                        !TargetTeam || TargetTeam->GetGenericTeamId().GetId() == SourceTeam ||
                        !SkillInstance->GetActiveSkillTargetResult().bIsValid || T->Attributes()->GetDefense() != 0.5f ||
                        T->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth)
                    { Fail(TEXT("RejectedEffectIdentityMagnitudeOrHealth")); return; }
                    const FVector Local = SkillInstance->GetActiveSkillTargetResult().HitQueryTransform.InverseTransformPosition(T->GetActorLocation());
                    if (!Local.Equals(RejectedOffset, 1.0)) { Fail(TEXT("RejectedTargetOutsideFixtureQuery")); return; }
                    UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP POSE Index=4 NetId=%llu X=%.6f Y=%.6f Z=%.6f Friendly=0 Defense=0.500000"),
                        NetId(T), Local.X, Local.Y, Local.Z);
                    UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP GE_CALLBACK Index=4 NetId=%llu Count=1 SetByCaller=250.000000 Class=GE_Shared_ApplyDamage_C Health=1000000.000000"), NetId(T));
                });
        }
        if (Spec->IsActive())
        {
            if (!Skill->GetActiveSkillTargetResult().bIsValid) return;
            const auto& Runtime = Skill->GetActiveSkillSpec();
            const auto* Attributes = ASC.GetSet<URPGAttributeSet>();
            if (!Attributes || Attributes->GetAttack() != 2.5f || Attributes->GetCriticalChance() != 0 ||
                Runtime.BaseDamage * Runtime.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"))) != 250.0f ||
                Runtime.SecurityProfile.MaximumFinalDamagePerHit != FixtureCap || !OriginalDefinition.IsValid() ||
                OriginalDefinition->SecurityProfile.MaximumFinalDamagePerHit != OriginalCap)
            { Fail(TEXT("FrozenCapOrAuthoredStatsMismatch")); return; }
            RejectedTarget->SetActorLocation(Skill->GetActiveSkillTargetResult().HitQueryTransform.TransformPosition(RejectedOffset));
            if (!bObservedActive)
            {
                bObservedActive = true;
                ActiveHandle = Handle;
                ActiveOriginal = Skill->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
                UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP ACTIVE SourceASCNetId=%llu Handle=%s Original=%d Cap=625.000000 SetByCaller=250.000000 Attack=2.500000 GlobalCap=%.6f"),
                    NetId(&ASC), *Handle.ToString(), ActiveOriginal, FRPGSkillSecurityProfile::MaximumSupportedFinalDamage);
            }
            return;
        }
        if (!bObservedActive) return;
        if (EndedAt < 0) EndedAt = Controller.GetWorld()->GetTimeSeconds();
        if (Controller.GetWorld()->GetTimeSeconds() - EndedAt < 3.0) return;
        if (AcceptedExecutions != 1 || RejectedExecutions != 1 || EffectCallbacks != 1 || HealthChanges != 0 ||
            !bObservedApplication || RejectedTarget->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth)
        { Fail(TEXT("FinalCapServerEvidenceIncomplete")); return; }
        RejectedTarget->bSuiteFinished = true;
        RejectedTarget->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP RESULT Role=Server Index=4 NetId=%llu Before=1000000.000000 After=1000000.000000 Callbacks=1 HealthChanges=0"), NetId(RejectedTarget.Get()));
        UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP SERVER_COMPLETE Targets=5 Accepted=1 Rejected=1"));
        bPassed = true;
    }
    else if (Controller.IsLocalController())
    {
        if (!RejectedTarget.IsValid() || !RejectedTarget->bInputReady || NetId(RejectedTarget.Get()) == 0 ||
            RejectedTarget->SourceASCNetId == 0 || RejectedTarget->SourceASCNetId != NetId(&ASC)) return;
        if (!PrepareProfile(*Skill, ASC, false)) return;
        if (!bClientBaseline)
        {
            if (RejectedTarget->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth) return;
            HealthHandle = RejectedTarget->ASC()->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentHealthAttribute()).AddLambda(
                [this](const FOnAttributeChangeData& Change)
                {
                    if (Change.NewValue != Change.OldValue) { ++HealthChanges; Fail(TEXT("RejectedClientHPChanged")); }
                });
            bClientBaseline = true;
            UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP BASELINE Index=4 NetId=%llu Health=1000000.000000"), NetId(RejectedTarget.Get()));
        }
        if (!RejectedTarget->bSuiteFinished || !AcceptedTarget->bSuiteFinished) return;
        if (HealthChanges != 0 || RejectedTarget->Attributes()->GetCurrentHealth() != ARPGSkillDamageNetworkTarget::BaselineHealth)
        { Fail(TEXT("RejectedClientFinalHealth")); return; }
        UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP RESULT Role=Client Index=4 NetId=%llu Before=1000000.000000 After=1000000.000000 HealthChanges=0"), NetId(RejectedTarget.Get()));
        UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP CLIENT_COMPLETE Targets=5 ReplicatedHealth=1"));
        bPassed = true;
    }
#endif
}

void FRPGSkillFinalCapNetworkProbe::ObserveExecution(const UAbilitySystemComponent& Source, const UAbilitySystemComponent& Target,
    const double Calculated, const double Cap, const bool bBound, const bool bApproved)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled() || !FailureReason.IsEmpty()) return;
    const auto* Skill = SourceSkill.Get();
    const bool bAcceptedTarget = AcceptedTarget.IsValid() && &Target == AcceptedTarget->ASC();
    const bool bRejectedTarget = RejectedTarget.IsValid() && &Target == RejectedTarget->ASC();
    if (&Source != SourceASC.Get() || !Skill || !Skill->IsActive() || !bObservedActive || !bBound || Cap != FixtureCap ||
        (!bAcceptedTarget && !bRejectedTarget) || bApproved != bAcceptedTarget || Calculated != (bAcceptedTarget ? 625.0 : 1250.0) ||
        Skill->GetCurrentActivationInfo().GetActivationPredictionKey().Current != ActiveOriginal)
    { Fail(TEXT("NativeFinalCapDecisionMismatch")); return; }
    int32& Count = bAcceptedTarget ? AcceptedExecutions : RejectedExecutions;
    if (++Count != 1) { Fail(TEXT("RepeatedNativeFinalCapExecution")); return; }
    UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP EXECUTION Index=%d SourceASCNetId=%llu NetId=%llu Handle=%s Original=%d Cap=625.000000 Calculated=%.6f Bound=1 Approved=%d"),
        bAcceptedTarget ? 0 : 4, NetId(&Source), NetId(Target.GetAvatarActor()), *ActiveHandle.ToString(), ActiveOriginal, Calculated, bApproved ? 1 : 0);
#endif
}

void FRPGSkillFinalCapNetworkProbe::ObserveApplication(URPGGameplayAbility_SkillContainer& Skill,
    const TArray<FRPGHitQueryResult>& Hits, const int32 AppliedCount, const int32 BudgetUsed)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled() || !FailureReason.IsEmpty()) return;
    if (&Skill != SourceSkill.Get() || !Skill.IsActive() || bObservedApplication || AppliedCount != 1 || Hits.Num() != 1 ||
        Hits[0].TargetActor != AcceptedTarget.Get() || BudgetUsed != 1 || AcceptedExecutions != 1 || RejectedExecutions != 1 ||
        EffectCallbacks != 1 || !RejectedTarget.IsValid() || Skill.GetActiveSkillSpec().SecurityProfile.MaximumFinalDamagePerHit != FixtureCap)
    { Fail(TEXT("RejectedHitLeakedIntoAppliedHitsOrBudget")); return; }
    bObservedApplication = true;
    UE_LOG(LogTemp, Display, TEXT("RPG_SKILL_FINALCAP APPLICATION SourceASCNetId=%llu Handle=%s Original=%d AppliedTargets=1 AppliedHits=1 BudgetUsed=1 BudgetMax=%d ApprovedNetId=%llu RejectedNetId=%llu"),
        NetId(SourceASC.Get()), *ActiveHandle.ToString(), ActiveOriginal, Skill.GetActiveSkillSpec().SecurityProfile.MaximumHitsPerActivation,
        NetId(AcceptedTarget.Get()), NetId(RejectedTarget.Get()));
#endif
}
