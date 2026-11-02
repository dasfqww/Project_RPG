#include "Tests/RPGInteractionInputTestTarget.h"
#include "Components/BoxComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

ARPGInteractionInputTestTarget::ARPGInteractionInputTestTarget()
{
    PrimaryActorTick.bCanEverTick = false;
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionFixture"));
    SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(15.f));
    Trigger->SetCollisionObjectType(ECC_WorldDynamic);
    Trigger->SetCollisionResponseToAllChannels(ECR_Overlap);
    Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool ARPGInteractionInputTestTarget::ConfigureForTest(float Duration, FText Label)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("RPGInteractionInputPIE")) || bConfigured
        || !FMath::IsFinite(Duration) || Duration <= 0.f) return false;
    InteractableData.InteractionDuration = Duration;
    InteractableData.Action = Label;
    bConfigured = true;
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    return true;
}

void ARPGInteractionInputTestTarget::BeginInteract()
{
    if (bConfigured) { bInteracting = true; ++BeginCount; }
}

void ARPGInteractionInputTestTarget::EndInteract()
{
    if (bInteracting) { bInteracting = false; ++CancelCount; }
}

void ARPGInteractionInputTestTarget::Interact(APlayerController*)
{
    if (bInteracting) { bInteracting = false; ++CompleteCount; }
}
