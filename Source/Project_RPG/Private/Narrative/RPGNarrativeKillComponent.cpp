#include "Narrative/RPGNarrativeKillComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Narrative/RPGNarrativeKillSubsystem.h"
#include "RPGGameplayTags.h"

URPGNarrativeKillComponent::URPGNarrativeKillComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void URPGNarrativeKillComponent::BeginPlay()
{
    Super::BeginPlay();
    auto* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner());
    if (!NPC || !NPC->HasAuthority() || CreditPolicy != ERPGNarrativeKillCreditPolicy::ConfirmedMembersInRadius
        || !FRPGNarrativeInteractionCommand::IsValidId(EnemyId) || !FMath::IsFinite(CreditRadius)
        || CreditRadius < 100.f || CreditRadius > 100000.f) return;
    if (auto* ASC = NPC->GetRPGAbilitySystemComponent())
    {
        DeathHandle = ASC->RegisterGameplayTagEvent(RPGGameplayTags::Shared_Status_Death, EGameplayTagEventType::NewOrRemoved)
            .AddUObject(this, &ThisClass::DeathTagChanged);
        NPC->OnNPCStartUpDataReady.AddUniqueDynamic(this, &ThisClass::Arm);
        Arm();
    }
}
void URPGNarrativeKillComponent::Arm()
{
    const auto* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner());
    const auto* ASC = NPC ? NPC->GetRPGAbilitySystemComponent() : nullptr;
    const auto* Attributes = NPC ? NPC->GetRPGAttributeSet() : nullptr;
    if (NPC && ASC && Attributes)
        Life.Arm(NPC->HasAuthority() && NPC->IsNPCStartUpDataReady(), Attributes->GetCurrentHealth(),
            ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death));
}
void URPGNarrativeKillComponent::DeathTagChanged(FGameplayTag Tag, const int32 Count)
{
    auto* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner());
    const auto* ASC = NPC ? NPC->GetRPGAbilitySystemComponent() : nullptr;
    const auto* Attributes = NPC ? NPC->GetRPGAttributeSet() : nullptr;
    if (Tag != RPGGameplayTags::Shared_Status_Death || Count <= 0 || !NPC || !ASC || !Attributes
        || ASC->GetAvatarActor() != NPC || CreditPolicy != ERPGNarrativeKillCreditPolicy::ConfirmedMembersInRadius
        || !Life.Consume(NPC->HasAuthority() && !NPC->IsActorBeingDestroyed() && NPC->IsNPCStartUpDataReady(),
            Attributes->GetCurrentHealth(), ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))) return;
    // Consume before queueing/HTTP/other callbacks. Removing/re-adding the tag cannot credit twice.
    if (auto* Game = NPC->GetGameInstance())
        if (auto* Outbox = Game->GetSubsystem<URPGNarrativeKillSubsystem>()) Outbox->RecordDeathInRadius(NPC, EnemyId, CreditRadius);
}
void URPGNarrativeKillComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner()))
    {
        NPC->OnNPCStartUpDataReady.RemoveDynamic(this, &ThisClass::Arm);
        if (auto* ASC = NPC->GetRPGAbilitySystemComponent(); ASC && DeathHandle.IsValid())
            ASC->RegisterGameplayTagEvent(RPGGameplayTags::Shared_Status_Death, EGameplayTagEventType::NewOrRemoved).Remove(DeathHandle);
    }
    Super::EndPlay(Reason);
}
