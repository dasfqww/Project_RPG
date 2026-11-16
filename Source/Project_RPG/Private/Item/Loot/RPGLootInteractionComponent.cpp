#include "Item/Loot/RPGLootInteractionComponent.h"
#include "Item/Loot/RPGTreasureChest.h"
#include "Item/Loot/RPGLootSourceComponent.h"
#include "Item/PickUp/RPGCurrencyPickup.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
URPGLootInteractionComponent::URPGLootInteractionComponent()
{ PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void URPGLootInteractionComponent::Request(AActor& Target, const FGuid& Identity)
{
    if (auto* PC = Cast<APlayerController>(GetOwner()); PC && (PC->HasAuthority() || PC->IsLocalController()))
        ServerInteract(&Target, Identity);
}
void URPGLootInteractionComponent::ServerInteract_Implementation(AActor* Target, FGuid Identity)
{
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!IsRegistered() || !IsValid(PC) || !PC->HasAuthority() || !GetWorld() ||
        !IsValid(Target) || Target->GetWorld() != GetWorld() || !Identity.IsValid() || GetWorld()->GetTimeSeconds() < NextRequest) return;
    NextRequest = GetWorld()->GetTimeSeconds() + .1;
    if (auto* Chest = Cast<ARPGTreasureChest>(Target)) Chest->LootSource->TryOpen(PC, Identity);
    else if (auto* Currency = Cast<ARPGCurrencyPickup>(Target); Currency && Currency->GetPickupDropId() == Identity) Currency->TryCollect(PC);
}
