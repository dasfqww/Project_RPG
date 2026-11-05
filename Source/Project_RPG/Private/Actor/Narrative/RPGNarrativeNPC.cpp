#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Manager/UIManager.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"

ARPGNarrativeNPC::ARPGNarrativeNPC()
{
    PrimaryActorTick.bCanEverTick = false;
    InteractionBounds = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionBounds"));
    SetRootComponent(InteractionBounds); InteractionBounds->InitSphereRadius(40.f);
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
    InteractionBounds->SetCollisionResponseToAllChannels(ECR_Overlap);
    Appearance = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Appearance"));
    Appearance->SetupAttachment(RootComponent); Appearance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nameplate = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Nameplate"));
    Nameplate->SetupAttachment(RootComponent); Nameplate->SetRelativeLocation(FVector(0, 0, 120));
    Nameplate->SetHorizontalAlignment(EHTA_Center); Nameplate->SetWorldSize(20.f);
    DisplayName = NSLOCTEXT("RPGNarrative", "NPCName", "안내인");
    InteractableData.InteractableType = EInteractableType::NPC;
    InteractableData.Action = NSLOCTEXT("RPGNarrative", "Talk", "대화하기");
}
void ARPGNarrativeNPC::BeginPlay()
{
    Super::BeginPlay(); InteractableData.Name = DisplayName; Nameplate->SetText(DisplayName);
}
void ARPGNarrativeNPC::BeginFocus() { Nameplate->SetTextRenderColor(FColor::Cyan); }
void ARPGNarrativeNPC::EndFocus() { Nameplate->SetTextRenderColor(FColor::White); }
bool ARPGNarrativeNPC::HasValidEntries() const
{
    if (DialogueEntryIds.IsEmpty() || DialogueEntryIds.Num() > 256) return false;
    TSet<FString> Seen;
    for (const auto& Id : DialogueEntryIds)
    {
        if (Id.IsEmpty() || Id.Len() > 128 || Seen.Contains(Id)) return false;
        for (TCHAR C : Id)
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')) return false;
        Seen.Add(Id);
    }
    return true;
}
bool ARPGNarrativeNPC::CanInteractWith(APlayerController* Controller) const
{
    const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    return !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown
        && IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController() && Controller->GetLocalPlayer()
        && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Controller->GetWorld() == GetWorld()
        && Pawn->GetWorld() == GetWorld() && HasValidEntries() && FMath::IsFinite(InteractionDistance)
        && !GetActorLocation().ContainsNaN() && !Pawn->GetActorLocation().ContainsNaN()
        && FVector::DistSquared(GetActorLocation(), Pawn->GetActorLocation()) <= FMath::Square(FMath::Clamp(InteractionDistance, 50.f, 1000.f));
}
URPGNarrativeDialogueWidget* ARPGNarrativeNPC::GetOpenDialogue(APlayerController* Controller) const
{
    const auto* Found = Controller ? OpenPanels.Find(Controller->GetLocalPlayer()) : nullptr;
    auto* Panel = Found ? Found->Get() : nullptr;
    return IsValid(Panel) && Panel->IsInViewport() ? Panel : nullptr;
}
void ARPGNarrativeNPC::Interact(APlayerController* Controller) { OpenDialogue(Controller); }
bool ARPGNarrativeNPC::OpenDialogue(APlayerController* Controller)
{
    if (!CanInteractWith(Controller) || !Controller->GetLocalPlayer()) return false;
    auto* Player = Controller->GetLocalPlayer();
    if (auto* Existing = GetOpenDialogue(Controller)) { Existing->FocusNPCInput(); return Existing->IsInViewport(); }
    auto* UI = UUIManager::Get(Player);
    auto* Game = Controller->GetGameInstance();
    auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    auto* Dungeon = Game ? Game->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
    auto* Flow = Player->GetSubsystem<URPGNarrativeCoordinator>();
    FGuid Character;
    if (!Web || !Web->HasAccessToken() || Web->GetAuthenticatedSteamId().IsEmpty() || !Dungeon || !Flow
        || UI->HasUIOpen() || Dungeon->IsRequestInFlight() || Dungeon->HasUnresolvedDeparture()
        || Dungeon->HasUnresolvedDungeonAbandonment() || Flow->IsBusy()
        || (Flow->HasPendingChoice() && !HasEntry(Flow->GetPendingDialogueId()))
        || !FGuid::Parse(Dungeon->GetSelectedCharacterId(), Character) || !Character.IsValid()) return false;
    auto* Panel = CreateWidget<URPGNarrativeDialogueWidget>(Controller);
    if (!Panel || !Panel->OpenForNPC(Character, this)) return false;
    OpenPanels.Add(Player, Panel); UI->PushUI(Panel);
    if (!Panel->IsInViewport()) { Panel->CloseScreen(); OpenPanels.Remove(Player); return false; }
    Panel->FocusNPCInput(); return Panel->IsInViewport();
}
void ARPGNarrativeNPC::EndPlay(const EEndPlayReason::Type Reason)
{
    auto Panels = MoveTemp(OpenPanels);
    for (const auto& Entry : Panels) if (auto* Panel = Entry.Value.Get()) Panel->CloseScreen();
    Super::EndPlay(Reason);
}
