#include "Tests/RPGSkillEndNetworkObservation.h"

#if !UE_BUILD_SHIPPING
#include "AbilitySystemComponent.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Tests/RPGSkillDamageNetworkProbe.h"

thread_local FRPGSkillEndNetworkObservation* FRPGSkillEndNetworkObservation::Current = nullptr;
namespace
{
bool Enabled()
{
    return IsInGameThread() && FParse::Param(FCommandLine::Get(), TEXT("RPGSkillNetE2E")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RPGSkillDamageE2E")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RPGSkillEndOriginE2E"));
}
uint64 NetId(const UAbilitySystemComponent* ASC)
{
    const UWorld* World = ASC ? ASC->GetWorld() : nullptr;
    const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
    const UReplicationSystem* System = Driver ? Driver->GetReplicationSystem() : nullptr;
    const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridge() : nullptr;
    return Bridge ? Bridge->GetReplicatedRefHandle(ASC).GetId() : 0;
}
}
#endif

FRPGSkillEndNetworkObservation::FRPGSkillEndNetworkObservation(const UAbilitySystemComponent& ASC,
    const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActivationInfo& Activation)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled()) return;
    Previous = Current;
    SourceASC = &ASC;
    SourceHandle = Handle;
    SourceKey = Activation.GetActivationPredictionKey();
    bInstalled = true;
    Current = this;
#endif
}

FRPGSkillEndNetworkObservation::~FRPGSkillEndNetworkObservation()
{
#if !UE_BUILD_SHIPPING
    if (bInstalled) Current = Previous;
#endif
}

void FRPGSkillEndNetworkObservation::ObserveEnd(const URPGGameplayAbility_SkillContainer& Skill,
    const UAbilitySystemComponent* ASC, const FGameplayAbilitySpecHandle Handle,
    const FGameplayAbilityActivationInfo& Activation, const bool bReplicate, const bool bCancelled)
{
#if !UE_BUILD_SHIPPING
    if (!Enabled() || Skill.GetClass()->GetName() != TEXT("GA_Player_ChargeSlash_C")) return;
    const AActor* Avatar = Skill.GetAvatarActorFromActorInfo();
    const bool bAuthority = Avatar && Avatar->HasAuthority();
    const bool bMatched = Current && Current->SourceASC == ASC && Current->SourceHandle == Handle &&
        Current->SourceKey == Activation.GetActivationPredictionKey();
    const TCHAR* Source = TEXT("Unknown");
    if (!Current && bReplicate) Source = bAuthority ? TEXT("AuthorityLocal") : TEXT("ClientLocal");
    else if (!bAuthority && bMatched && !bReplicate && !bCancelled) Source = TEXT("AuthorityRPC");
    UE_LOG(LogTemp, Display, TEXT("%sRPG_SKILL_END_ORIGIN Role=%s ASCNetId=%llu Handle=%s Original=%d Source=%s RpcMatched=%d RpcOriginal=%d"),
        *FRPGSkillDamageNetworkProbe::LogContext(Avatar), bAuthority ? TEXT("Server") : TEXT("Client"),
        NetId(ASC), *Handle.ToString(), Activation.GetActivationPredictionKey().Current, Source,
        bMatched ? 1 : 0, Current ? Current->SourceKey.Current : 0);
#endif
}
