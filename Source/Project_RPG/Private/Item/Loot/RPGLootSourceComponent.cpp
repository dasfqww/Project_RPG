#include "Item/Loot/RPGLootSourceComponent.h"
#include "Item/Loot/RPGWorldLoot.h"
#include "AbilitySystemComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Pawn.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "RPGGameplayTags.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "Attribute/RPGAttributeSet.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGLootSourceComponent::URPGLootSourceComponent()
{ PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void URPGLootSourceComponent::OnRegister()
{
    Super::OnRegister();
    if (GetOwner() && GetOwner()->HasAuthority() && !GetOwner()->IsTemplate() && !SourceId.IsValid()) SourceId = FGuid::NewGuid();
}
void URPGLootSourceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(URPGLootSourceComponent, SourceId); DOREPLIFETIME(URPGLootSourceComponent, UsedEvents); }
bool URPGLootSourceComponent::TryOpen(APlayerController* Controller, const FGuid& ExpectedId)
{
    return Trigger == ERPGLootSourceTrigger::Open && UsedEvents == 0 && SourceId.IsValid() && SourceId == ExpectedId &&
        GetOwner() && RPGWorldLoot::Collector(*GetOwner(), Controller) && Emit();
}
void URPGLootSourceComponent::NotifyDamage(const FGameplayEffectModCallbackData& Data, const float PreviousHealth, const float CurrentHealth)
{
    auto* NPC = Cast<ARPGNonPlayerCharacter>(GetOwner());
    auto* InstigatorASC = Data.EffectSpec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
    auto* Instigator = InstigatorASC ? Cast<APawn>(InstigatorASC->GetAvatarActor()) : nullptr;
    auto* Controller = Instigator ? Cast<APlayerController>(Instigator->GetController()) : nullptr;
    if (Trigger != ERPGLootSourceTrigger::Damage || !IsValid(NPC) || !NPC->HasAuthority() || NPC->IsActorBeingDestroyed() ||
        NPC->GetRPGAbilitySystemComponent() != &Data.Target || Data.Target.GetAvatarActor() != NPC || Instigator == NPC ||
        Data.EvaluatedData.Attribute != URPGAttributeSet::GetTakeDamageAttribute() ||
        Data.Target.HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) || IsDungeonGameplayRevoked(NPC) ||
        !FMath::IsFinite(PreviousHealth) || !FMath::IsFinite(CurrentHealth) || PreviousHealth <= 0 || CurrentHealth < 0 || CurrentHealth >= PreviousHealth ||
        !FMath::IsFinite(DamageInterval) || DamageInterval < .05f || DamageInterval > 60.f ||
        !URPGCombatFunctionLibrary::IsTargetPawnHostile(Instigator, NPC) ||
        !RPGWorldLoot::Collector(*NPC, Controller, 3000.f) || !GetWorld() || GetWorld()->GetTimeSeconds() < NextDamageTime) return;
    // Reserve before any spawn callback, including another valid damage execution.
    NextDamageTime = GetWorld()->GetTimeSeconds() + DamageInterval;
    Emit();
}
bool URPGLootSourceComponent::Emit()
{
    AActor* Owner = GetOwner();
    if (!IsRegistered() || !IsValid(Owner) || !Owner->HasAuthority() || Owner->IsActorBeingDestroyed() ||
        !Owner->GetWorld() || Owner->GetWorld()->bIsTearingDown || Owner->GetActorLocation().ContainsNaN() ||
        MaximumEvents < 1 || MaximumEvents > 100 || UsedEvents >= MaximumEvents) return false;
    TArray<FDropItem> Drops;
    if (!RPGWorldLoot::ReadDrops(DropTable, DropRow, Drops)) return false;
    ++UsedEvents; Owner->ForceNetUpdate();
    RPGWorldLoot::Spawn(*Owner, Drops); return true;
}
