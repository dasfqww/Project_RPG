#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "Narrative/RPGNarrativeInvestigationComponent.h"
#include "Narrative/RPGNarrativeInteraction.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

ARPGNarrativeInvestigation::ARPGNarrativeInvestigation()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = false;
    InteractionBounds = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionBounds"));
    SetRootComponent(InteractionBounds); InteractionBounds->InitSphereRadius(40.f);
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionBounds->SetCollisionResponseToAllChannels(ECR_Overlap);
    Appearance = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Appearance"));
    Appearance->SetupAttachment(RootComponent); Appearance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Nameplate"));
    Nameplate->SetupAttachment(RootComponent); Nameplate->SetRelativeLocation(FVector(0, 0, 100));
    Nameplate->SetHorizontalAlignment(EHTA_Center); Nameplate->SetWorldSize(18.f);
    DisplayName = NSLOCTEXT("RPGNarrative", "InvestigationName", "조사 대상");
    InteractableData.InteractableType = EInteractableType::Device;
    InteractableData.InteractionDuration = 0.f;
    InteractableData.Action = NSLOCTEXT("RPGNarrative", "Investigate", "조사하기");
}
void ARPGNarrativeInvestigation::BeginPlay()
{
    Super::BeginPlay(); InteractableData.Name = DisplayName; Nameplate->SetText(DisplayName);
    // This actor always uses immediate server approval, regardless of a Blueprint's legacy duration setting.
    InteractableData.InteractionDuration = 0.f;
}
void ARPGNarrativeInvestigation::BeginFocus() { Nameplate->SetTextRenderColor(FColor::Cyan); }
void ARPGNarrativeInvestigation::EndFocus() { Nameplate->SetTextRenderColor(FColor::White); }
void ARPGNarrativeInvestigation::SetLocalStatus(const FText& Status, const bool bRetry)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    Nameplate->SetText(Status.IsEmpty() ? DisplayName : FText::Format(NSLOCTEXT("RPGNarrative", "InvestigationStatus", "{0}\n{1}"), DisplayName, Status));
    InteractableData.Action = bRetry ? NSLOCTEXT("RPGNarrative", "RetryInvestigation", "진행 결과 다시 확인")
        : NSLOCTEXT("RPGNarrative", "Investigate", "조사하기");
}
bool ARPGNarrativeInvestigation::CanObserve(APlayerController* Controller) const
{
    const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    if (IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown || !IsValid(Pawn)
        || Pawn->IsActorBeingDestroyed() || Pawn->GetWorld() != GetWorld() || Controller->GetWorld() != GetWorld()
        || !FRPGNarrativeInteractionCommand::IsValidId(TargetId) || !FMath::IsFinite(InteractionDistance)
        || GetActorLocation().ContainsNaN() || Pawn->GetActorLocation().ContainsNaN()
        || FVector::DistSquared(GetActorLocation(), Pawn->GetActorLocation()) > FMath::Square(FMath::Clamp(InteractionDistance, 50.f, 1000.f))) return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NarrativeInvestigation), false, Pawn);
    Params.AddIgnoredActor(this);
    return !GetWorld()->LineTraceTestByChannel(Pawn->GetPawnViewLocation(), GetActorLocation(), ECC_Visibility, Params);
}
void ARPGNarrativeInvestigation::Interact(APlayerController* Controller)
{
    if (!IsValid(Controller) || !Controller->IsLocalController()) return;
    if (auto* Component = Controller->FindComponentByClass<URPGNarrativeInvestigationComponent>()) Component->Investigate(this);
}
