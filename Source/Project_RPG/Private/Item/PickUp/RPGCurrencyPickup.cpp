#include "Item/PickUp/RPGCurrencyPickup.h"
#include "Item/Loot/RPGWorldLoot.h"
#include "Item/Loot/RPGCurrencyLootJournal.h"
#include "Item/Loot/RPGLootInteractionComponent.h"
#include "Economy/Backend/RPGEconomyBackendSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Async/Async.h"

ARPGCurrencyPickup::ARPGCurrencyPickup()
{
    InstanceInteractableData.Action = NSLOCTEXT("Loot", "CollectCurrency", "획득");
}
ARPGCurrencyPickup::~ARPGCurrencyPickup() = default;
bool ARPGCurrencyPickup::CanInitializeServerDropQuantity(const int32 Quantity) const
{
    if (CurrencyCode.IsNone() || CurrencyCode.ToString().Len() > 64 || AmountPerUnit <= 0 || Quantity <= 0 || Quantity > MAX_int8 ||
        AmountPerUnit > MAX_int64 / Quantity) return false;
    for (TCHAR C : CurrencyCode.ToString())
        if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')) return false;
    return true;
}
bool ARPGCurrencyPickup::InitializeServerDropQuantity(const int32 Quantity)
{
    if (!HasAuthority() || IsActorBeingDestroyed() || !bIsAvailableForPickup || IsHidden() || Frozen.RequestId.IsValid() ||
        !CanInitializeServerDropQuantity(Quantity)) return false;
    Amount = Quantity * AmountPerUnit; ItemQuantity = Quantity;
    InstanceInteractableData.Quantity = Quantity; OnRep_Amount(); ForceNetUpdate(); return true;
}
void ARPGCurrencyPickup::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && Amount == 0 && CanInitializeServerDropQuantity(1)) Amount = AmountPerUnit;
    OnRep_Amount();
}
void ARPGCurrencyPickup::OnRep_Amount()
{
    InteractableData = InstanceInteractableData;
    InteractableData.Name = FText::Format(NSLOCTEXT("Loot", "CurrencyAmount", "{0} {1}"),
        FText::FromName(CurrencyCode), FText::AsNumber(Amount));
}
void ARPGCurrencyPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ARPGCurrencyPickup, Amount); }
void ARPGCurrencyPickup::TakePickup(APlayerController* Controller)
{
    if (IsValid(Controller)) if (auto* Bridge = Controller->FindComponentByClass<URPGLootInteractionComponent>())
        Bridge->Request(*this, GetPickupDropId());
}
bool ARPGCurrencyPickup::TryCollect(APlayerController* Controller)
{
    auto* State = RPGWorldLoot::Collector(*this, Controller);
    auto* Economy = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGEconomyBackendSubsystem>() : nullptr;
    if (!State || !Economy || !Economy->IsAvailable() || bEnding || bInFlight || Amount <= 0) return false;
    if (Frozen.RequestId.IsValid())
    {
        if (Controller != Claimant || !IsClaimantCurrent()) return false;
        Send(); return true;
    }
    if (!CanSubmitPickup(Controller) || !GetPickupDropId().IsValid()
        || !CanInitializeServerDropQuantity(ItemQuantity > 0 ? ItemQuantity : 1) || !TryClaimPickup()) return false;
    Frozen.RequestId = GetPickupDropId(); Frozen.CharacterId = State->GetBackendCharacterId();
    Frozen.DungeonSessionId = State->GetBackendDungeonSessionId(); Frozen.Operation = TEXT("WorldLoot");
    Frozen.CommandFingerprint = TEXT("world-loot:") + GetPickupDropId().ToString(EGuidFormats::DigitsWithHyphensLower);
    Frozen.Reason = TEXT("World.Loot.Currency");
    FRPGCurrencyChange Change; Change.CurrencyCode = CurrencyCode; Change.Delta = Amount; Frozen.Changes.Add(Change);
    Journal = MakeUnique<FRPGCurrencyLootJournal>();
    if (!Journal->Create(FPaths::ProjectSavedDir() / TEXT("CurrencyLoot"), Frozen))
    { Journal.Reset(); Frozen = {}; ReleasePickupClaim(); return false; }
    Claimant = Controller; ClaimantState = State; ClaimantAccount = State->GetAuthenticatedSteamId();
    ConnectionGeneration = State->GetBackendConnectionGeneration(); Send(); return true;
}
bool ARPGCurrencyPickup::IsClaimantCurrent() const
{
    auto* PC = Claimant.Get(); auto* State = ClaimantState.Get();
    return IsValid(PC) && IsValid(State) && PC->GetPlayerState<ARPGPlayerState>() == State &&
        RPGWorldLoot::Collector(*this, PC) == State && State->GetBackendCharacterId() == Frozen.CharacterId &&
        State->GetBackendDungeonSessionId() == Frozen.DungeonSessionId && State->GetAuthenticatedSteamId() == ClaimantAccount &&
        State->GetBackendConnectionGeneration() == ConnectionGeneration;
}
void ARPGCurrencyPickup::Send()
{
    if (bEnding || bInFlight || !Frozen.RequestId.IsValid() || !IsClaimantCurrent()) return;
    if (bConfirmed)
    {
        ++Attempts;
        if (Journal && Journal->Clear()) { Journal.Reset(); PickedUp(); }
        else ScheduleRetry();
        return;
    }
    auto* Economy = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGEconomyBackendSubsystem>() : nullptr;
    if (!Economy || !Economy->IsAvailable()) { ScheduleRetry(); return; }
    bInFlight = true; ++Attempts;
    Economy->Commit(Frozen, [Weak=TWeakObjectPtr<ARPGCurrencyPickup>(this)](FRPGEconomyCommitResult Result) mutable
    {
        auto Apply = [Weak, Result=MoveTemp(Result)]() mutable { if (Weak.IsValid()) Weak->Receive(MoveTemp(Result)); };
        if (IsInGameThread()) Apply(); else AsyncTask(ENamedThreads::GameThread, MoveTemp(Apply));
    });
}
void ARPGCurrencyPickup::Receive(FRPGEconomyCommitResult Result)
{
    bInFlight = false;
    if (bEnding) return;
    const bool bReceipt = Result.WasSuccessful() && Result.HttpStatusCode >= 200 && Result.HttpStatusCode < 300 &&
        Result.RequestId == Frozen.RequestId && Result.CharacterId == Frozen.CharacterId &&
        Result.Operation == Frozen.Operation && Result.CommandFingerprint == Frozen.CommandFingerprint && Result.Reason == Frozen.Reason &&
        Result.Changes.Num() == 1 && Result.Changes[0].CurrencyCode == Frozen.Changes[0].CurrencyCode && Result.Changes[0].Delta == Frozen.Changes[0].Delta;
    // Even authorization/limit errors after an earlier timeout cannot prove that earlier POST did not commit.
    if (bReceipt)
    {
        bConfirmed = true;
        if (Journal && Journal->Clear()) { Journal.Reset(); PickedUp(); return; }
    }
    ScheduleRetry();
}
void ARPGCurrencyPickup::ScheduleRetry()
{
    if (!bEnding && GetWorld() && Attempts < 5 && IsClaimantCurrent())
        GetWorld()->GetTimerManager().SetTimer(RetryTimer, this, &ThisClass::Send, static_cast<float>(1 << FMath::Clamp(Attempts, 1, 4)), false);
}
void ARPGCurrencyPickup::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true; if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
    Journal.Reset(); // Original pending body survives shutdown. No new identity or recipient is allocated.
    Super::EndPlay(Reason);
}
