#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Manager/UIManager.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGDialogueManager.h"
#include "UI/Narrative/RPGNarrativeNPCMarkerWidget.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "Riding/RPGRidingLocalInteractionComponent.h"

ARPGNarrativeNPC::ARPGNarrativeNPC()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = .5f;
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
    if (GetNetMode() == NM_DedicatedServer) SetActorTickEnabled(false);
    else RefreshPersonalMarkers();
}
void ARPGNarrativeNPC::Tick(float DeltaSeconds) { Super::Tick(DeltaSeconds); RefreshPersonalMarkers(); }
void ARPGNarrativeNPC::RefreshPersonalMarkers()
{
    if (GetNetMode() == NM_DedicatedServer || !GetWorld() || GetWorld()->bIsTearingDown) return;
    auto* Game = GetGameInstance(); if (!Game) return;
    for (auto It = PersonalMarkers.CreateIterator(); It; ++It)
    {
        auto* Player = It.Key().Get(); auto* Component = It.Value().Get();
        auto* PC = Player ? Player->GetPlayerController(GetWorld()) : nullptr;
        auto* Widget = Component ? Component->GetUserWidgetObject() : nullptr;
        if (!bShowPersonalQuestMarker || !Player || !Game->GetLocalPlayers().Contains(Player) || !PC || !Widget || Widget->GetOwningPlayer() != PC)
        {
            if (Component) { RemoveInstanceComponent(Component); Component->DestroyComponent(); }
            It.RemoveCurrent();
        }
    }
    if (!bShowPersonalQuestMarker || !HasValidEntries()) return;
    for (const auto& PlayerEntry : Game->GetLocalPlayers())
    {
        ULocalPlayer* Player = PlayerEntry; auto* PC = Player ? Player->GetPlayerController(GetWorld()) : nullptr;
        if (!PC || !PC->IsLocalController() || PersonalMarkers.Contains(Player)) continue;
        auto* Widget = CreateWidget<URPGNarrativeNPCMarkerWidget>(PC); if (!Widget) continue;
        const TWeakObjectPtr<ThisClass> WeakThis(this); const TWeakObjectPtr<ULocalPlayer> WeakPlayer(Player);
        Widget->Configure([WeakThis, WeakPlayer](TArray<FString>& Entries, TArray<FRPGDialogueNode>& Nodes)
        {
            const auto* NPC = WeakThis.Get(); auto* Local = WeakPlayer.Get();
            auto* Controller = Local && NPC ? Local->GetPlayerController(NPC->GetWorld()) : nullptr;
            const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
            const auto* Flow = Local ? Local->GetSubsystem<URPGNarrativeCoordinator>() : nullptr;
            const auto* Instance = Local ? Local->GetGameInstance() : nullptr;
            const auto* Dungeon = Instance ? Instance->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr; FGuid Selected;
            if (!NPC || NPC->IsActorBeingDestroyed() || !NPC->bShowPersonalQuestMarker || !NPC->HasValidEntries()
                || !Controller || !Controller->IsLocalController() || !Pawn || Pawn->IsActorBeingDestroyed()
                || Pawn->GetWorld() != NPC->GetWorld() || !FMath::IsFinite(NPC->QuestMarkerDistance)
                || NPC->GetActorLocation().ContainsNaN() || Pawn->GetActorLocation().ContainsNaN()
                || FVector::DistSquared(NPC->GetActorLocation(),Pawn->GetActorLocation()) > FMath::Square(FMath::Clamp(NPC->QuestMarkerDistance,50.f,10000.f))
                || !Flow || !Dungeon || !FGuid::Parse(Dungeon->GetSelectedCharacterId(),Selected)
                || Flow->GetSelectedCharacterId() != Selected || !Flow->HasCurrentSnapshot()) return false;
            Entries = NPC->DialogueEntryIds; Nodes = Flow->GetDialogueManager()->GetAvailableDialogues(); return true;
        });
        auto* Component = NewObject<UWidgetComponent>(this, NAME_None, RF_Transient);
        AddInstanceComponent(Component); Component->SetupAttachment(RootComponent); Component->SetRelativeLocation(FVector(0,0,150));
        Component->SetWidgetSpace(EWidgetSpace::Screen); Component->SetOwnerPlayer(Player); Component->SetDrawAtDesiredSize(true);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); Component->SetIsReplicated(false); Component->SetWidget(Widget);
        Component->RegisterComponent(); PersonalMarkers.Add(Player, Component);
    }
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
    return CanApproveRidingLocalInteraction(Controller) && Controller->IsLocalController() && Controller->GetLocalPlayer();
}
FGuid ARPGNarrativeNPC::GetRidingLocalInteractionIdentity() const
{
    return HasValidEntries() ? MakeRidingLocalInteractionIdentity(FString::Join(DialogueEntryIds, TEXT("|"))) : FGuid{};
}
bool ARPGNarrativeNPC::CanApproveRidingLocalInteraction(APlayerController* Controller) const
{
    const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
    return !IsActorBeingDestroyed() && GetWorld() && !GetWorld()->bIsTearingDown
        && IsValid(Controller) && !Controller->IsActorBeingDestroyed()
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
void ARPGNarrativeNPC::Interact(APlayerController* Controller)
{
    if (!CanOpenDialogue(Controller)) return;
    if (URPGRidingLocalInteractionComponent::NeedsApproval(Controller))
    {
        if (auto* Gate = Controller->FindComponentByClass<URPGRidingLocalInteractionComponent>())
            Gate->Request(*this, [Self = TWeakObjectPtr<ARPGNarrativeNPC>(this), Owner = TWeakObjectPtr<APlayerController>(Controller)]
                { if (Self.IsValid() && Owner.IsValid()) Self->Interact(Owner.Get()); });
        return;
    }
    OpenDialogue(Controller);
}
bool ARPGNarrativeNPC::CanOpenDialogue(APlayerController* Controller) const
{
    if (!CanInteractWith(Controller) || !Controller->GetLocalPlayer()) return false;
    auto* Player = Controller->GetLocalPlayer();
    if (GetOpenDialogue(Controller)) return true;
    auto* UI = UUIManager::Get(Player);
    auto* Game = Controller->GetGameInstance();
    auto* Web = Game ? Game->GetSubsystem<UHttpWebManager>() : nullptr;
    auto* Dungeon = Game ? Game->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
    auto* Flow = Player->GetSubsystem<URPGNarrativeCoordinator>();
    FGuid Character;
    if (!UI || !Web || !Web->HasAccessToken() || Web->GetAuthenticatedSteamId().IsEmpty() || !Dungeon || !Flow
        || UI->HasUIOpen() || Dungeon->IsRequestInFlight() || Dungeon->HasUnresolvedDeparture()
        || Dungeon->HasUnresolvedDungeonAbandonment() || Flow->IsBusy()
        || (Flow->HasPendingChoice() && !Flow->HasPendingDungeonObjective() && !HasEntry(Flow->GetPendingDialogueId()))
        || !FGuid::Parse(Dungeon->GetSelectedCharacterId(), Character) || !Character.IsValid()) return false;
    return true;
}
bool ARPGNarrativeNPC::OpenDialogue(APlayerController* Controller)
{
    if (!CanOpenDialogue(Controller) || URPGRidingLocalInteractionComponent::NeedsApproval(Controller)) return false;
    auto* Player = Controller->GetLocalPlayer();
    if (auto* Existing = GetOpenDialogue(Controller)) { Existing->FocusNPCInput(); return Existing->IsInViewport(); }
    auto* UI = UUIManager::Get(Player);
    auto* Dungeon = Controller->GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
    FGuid Character;
    if (!Dungeon || !FGuid::Parse(Dungeon->GetSelectedCharacterId(), Character)) return false;
    auto* Panel = CreateWidget<URPGNarrativeDialogueWidget>(Controller);
    if (!Panel || !Panel->OpenForNPC(Character, this)) return false;
    OpenPanels.Add(Player, Panel); UI->PushUI(Panel);
    if (!Panel->IsInViewport()) { Panel->CloseScreen(); OpenPanels.Remove(Player); return false; }
    Panel->FocusNPCInput(); return Panel->IsInViewport();
}
void ARPGNarrativeNPC::EndPlay(const EEndPlayReason::Type Reason)
{
    for (const auto& Entry : PersonalMarkers) if (auto* Component = Entry.Value.Get())
    { RemoveInstanceComponent(Component); Component->DestroyComponent(); }
    PersonalMarkers.Reset();
    auto Panels = MoveTemp(OpenPanels);
    for (const auto& Entry : Panels) if (auto* Panel = Entry.Value.Get()) Panel->CloseScreen();
    Super::EndPlay(Reason);
}
