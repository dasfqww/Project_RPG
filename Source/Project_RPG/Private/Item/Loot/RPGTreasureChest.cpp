#include "Item/Loot/RPGTreasureChest.h"
#include "Item/Loot/RPGLootSourceComponent.h"
#include "Item/Loot/RPGLootInteractionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
ARPGTreasureChest::ARPGTreasureChest()
{
    bReplicates = true; PrimaryActorTick.bCanEverTick = false;
    ChestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ChestMesh")); SetRootComponent(ChestMesh);
    LootSource = CreateDefaultSubobject<URPGLootSourceComponent>(TEXT("LootSource"));
    InteractableData.InteractableType = EInteractableType::Container;
    InteractableData.Name = NSLOCTEXT("Loot", "Chest", "보물상자");
    InteractableData.Action = NSLOCTEXT("Loot", "Open", "열기");
}
void ARPGTreasureChest::Interact(APlayerController* Controller)
{
    if (!IsValid(Controller)) return;
    if (auto* Bridge = Controller->FindComponentByClass<URPGLootInteractionComponent>())
        Bridge->Request(*this, LootSource->GetSourceId());
}
