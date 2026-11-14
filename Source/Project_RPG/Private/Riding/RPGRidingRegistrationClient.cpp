#include "Riding/RPGRidingRegistrationClient.h"
#include "Riding/RPGRidingIntentJson.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"
#include "HAL/PlatformTime.h"

FRPGRidingRegistrationClient::FRPGRidingRegistrationClient(TSharedRef<FRPGRidingCollectionReader> C, TSharedRef<FRPGRidingRegistrationReader> D,
    TSharedRef<IRPGRidingOwnerTransport> B, TSharedRef<FRPGRidingCommandCoordinator> W, FRPGRidingCollectionReader::FContext Read,
    FNames N, TFunction<bool()> Refresh)
    : Collection(MoveTemp(C)), Definitions(MoveTemp(D)), Bag(MoveTemp(B)), Commands(MoveTemp(W)), Context(MoveTemp(Read)), Names(MoveTemp(N)), RefreshAction(MoveTemp(Refresh))
{
    CollectionHandle = Collection->OnChanged.AddRaw(this, &FRPGRidingRegistrationClient::CollectionChanged);
    DefinitionsHandle = Definitions->OnChanged.AddRaw(this, &FRPGRidingRegistrationClient::DefinitionsChanged);
    CommandsHandle = Commands->OnChanged.AddRaw(this, &FRPGRidingRegistrationClient::CommandsChanged);
}
FRPGRidingRegistrationClient::~FRPGRidingRegistrationClient() { Shutdown(); }
void FRPGRidingRegistrationClient::Shutdown()
{
    bEnding = true; Collection->OnChanged.Remove(CollectionHandle); Definitions->OnChanged.Remove(DefinitionsHandle);
    Commands->OnChanged.Remove(CommandsHandle); Clear(); Bound = {};
    // Never cancel, dispose or replace the persistent command owner when a screen closes.
}
void FRPGRidingRegistrationClient::Clear()
{ Pending.Invalidate(); Bag->Cancel(); Snapshot.Invalidate(); Records.Reset(); Options = {}; Phase = EPhase::Idle; Dismiss(); }
void FRPGRidingRegistrationClient::Dismiss() { Consent.Invalidate(); ReviewedItem.Invalidate(); ItemName = {}; MountName = {}; ConsentText = {}; }
void FRPGRidingRegistrationClient::RefreshContext()
{
    if (bEnding || bRefreshing || !IsInGameThread()) return; TGuardValue<bool> Guard(bRefreshing, true);
    auto C = Context ? Context() : FRPGRidingCollectionContext{}; FString Endpoint, Account;
    C.bEligible &= C.Character.IsValid() && Bag->HasCredential() && Bag->GetContext(Endpoint, Account) && C.Endpoint == Endpoint && C.Account == Account;
    if (!Bound.SameIdentity(C) || Bound.bEligible != C.bEligible) { Bound = C; RetryAt = 0; Clear(); }
    Commands->RefreshContext(); Collection->RefreshContext(); Definitions->RefreshContext();
}
bool FRPGRidingRegistrationClient::Usable()
{ return !bEnding && IsInGameThread() && Bound.bEligible && Commands->GetState() != ERPGRidingCommandState::Unavailable; }
bool FRPGRidingRegistrationClient::Ready()
{
    const auto* C = Collection->GetCollection(); const auto* D = Definitions->GetOptions();
    return Usable() && !Commands->BlocksTransitions() && Phase == EPhase::Ready && Snapshot.IsValid()
        && FPlatformTime::Seconds() - ReceivedAt < 60 && C && D && Options.MatchesCollection(*C)
        && D->RosterId == Options.RosterId && D->RosterRevision == Options.RosterRevision && D->GameplayHash == Options.GameplayHash;
}
bool FRPGRidingRegistrationClient::Reload()
{
    RefreshContext();
    if (!Usable() || Commands->BlocksTransitions() || Phase == EPhase::Collection || Phase == EPhase::Definitions || Phase == EPhase::Inventory
        || FPlatformTime::Seconds() < RetryAt || Collection->GetRetryAfterSeconds() > 0 || Definitions->GetRetryAfterSeconds() > 0) return false;
    Clear(); Phase = EPhase::Collection;
    const bool Sent = Collection->Read(); if (!Sent && Phase == EPhase::Collection) Fail();
    OnChanged.Broadcast(); return Sent;
}
void FRPGRidingRegistrationClient::Fail() { Clear(); Phase = EPhase::Failed; OnChanged.Broadcast(); }
void FRPGRidingRegistrationClient::CollectionChanged()
{
    if (bEnding) return;
    if (Phase != EPhase::Collection) { if (Phase == EPhase::Definitions || Phase == EPhase::Inventory || Phase == EPhase::Ready) Clear(); OnChanged.Broadcast(); return; }
    const auto State = Collection->GetState();
    if (State == ERPGRidingCollectionReadState::Loading || State == ERPGRidingCollectionReadState::Empty) return;
    const auto* C = Collection->GetCollection(); if (!C || !Usable() || Commands->BlocksTransitions()) { Fail(); return; }
    Phase = EPhase::Definitions; if (!Definitions->Read() && Phase == EPhase::Definitions) Fail();
    OnChanged.Broadcast();
}
void FRPGRidingRegistrationClient::DefinitionsChanged()
{
    if (bEnding) return;
    if (Phase != EPhase::Definitions) { if (Phase == EPhase::Inventory || Phase == EPhase::Ready) Clear(); OnChanged.Broadcast(); return; }
    const auto State = Definitions->GetState();
    if (State == ERPGRidingCollectionReadState::Loading || State == ERPGRidingCollectionReadState::Empty) return;
    const auto* D = Definitions->GetOptions(); const auto* C = Collection->GetCollection();
    if (!D || !C || !D->MatchesCollection(*C) || !Usable() || Commands->BlocksTransitions()) { Fail(); return; }
    Options = *D; Phase = EPhase::Inventory; const auto Request = Pending = FGuid::NewGuid(); const auto Captured = Bound;
    const TWeakPtr<FRPGRidingRegistrationClient> Weak = AsShared();
    const bool Sent = Bag->ReadInventory(Bound.Character, [Weak, Request, Captured](FRPGRidingOwnerHttpResult R)
    { if (const auto Self = Weak.Pin()) Self->CompleteBag(Request, Captured, MoveTemp(R)); });
    if (!Sent && Pending == Request) Fail(); OnChanged.Broadcast();
}
void FRPGRidingRegistrationClient::CompleteBag(FGuid Request, FRPGRidingCollectionContext Captured, FRPGRidingOwnerHttpResult R)
{
    RefreshContext(); if (bEnding || Request != Pending || !Bound.SameIdentity(Captured) || !Usable() || Commands->BlocksTransitions()) return;
    Pending.Invalidate(); TArray<FRPGItemRecord> Parsed; TArray<FRPGInventoryProjectionEntry> Items, Mail;
    const FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, Bound.Character.ToString(EGuidFormats::DigitsWithHyphensLower)};
    const auto* C = Collection->GetCollection();
    if (!R.bCurrentPlayerContext || !R.bTransportSuccessful || R.StatusCode != 200 || R.Body.Len() > 1024 * 1024 || !RPGRidingIntentJson::UniqueJson(R.Body)
        || !FRPGItemBackendJsonCodec::DeserializeLoadResponse(R.Body, Parsed) || Parsed.Num() >= 500
        || !FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner, Parsed, Items) || !FRPGInventoryProjectionMapper::BuildMailSnapshot(Owner, Parsed, Mail)
        || !C || !Options.MatchesCollection(*C))
    {
        if (R.bCurrentPlayerContext && (R.StatusCode == 429 || R.StatusCode == 503)) RetryAt = FPlatformTime::Seconds()
            + (FMath::IsFinite(R.RetryAfterSeconds) ? FMath::Clamp(R.RetryAfterSeconds, 1., 3600.) : 60.);
        Fail(); return;
    }
    Records = MoveTemp(Parsed); Records.Sort([](const auto& A, const auto& B) { return A.GetLocation().SlotIndex < B.GetLocation().SlotIndex; });
    Snapshot = FGuid::NewGuid(); ReceivedAt = FPlatformTime::Seconds(); Phase = EPhase::Ready; OnChanged.Broadcast();
}
void FRPGRidingRegistrationClient::CommandsChanged() { if (!bEnding) { Clear(); OnChanged.Broadcast(); } }
bool FRPGRidingRegistrationClient::Candidate(const FRPGItemRecord& R, const FRPGRidingRegistrationOption*& Option) const
{
    Option = Options.Find(R.GetDefinitionId().PrimaryAssetType.ToString(), R.GetDefinitionId().PrimaryAssetName.ToString(), R.GetDefinitionVersion());
    const FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, Bound.Character.ToString(EGuidFormats::DigitsWithHyphensLower)};
    return Options.bRegistrationEnabled && Option && !Option->bAlreadyOwned && R.CanBeMutatedBy(Owner, FDateTime::UtcNow())
        && R.GetLocation().ContainerType == ERPGItemContainerType::Inventory && R.GetQuantity() >= 1 && R.GetQuantity() <= Option->MaximumStackSize
        && R.GetRevision() > 0 && R.GetRevision() < MAX_int64;
}
bool FRPGRidingRegistrationClient::Review(FGuid S, FGuid Item)
{
    RefreshContext(); Dismiss(); if (!Ready() || S != Snapshot) return false;
    const auto* R = Records.FindByPredicate([&](const auto& V) { return V.GetItemId() == Item; }); const FRPGRidingRegistrationOption* Option = nullptr;
    if (!R || !Candidate(*R, Option) || !Names || !Names(*R, *Option, ItemName, MountName) || ItemName.IsEmpty() || MountName.IsEmpty()) return false;
    ReviewedItem = Item; Consent = FGuid::NewGuid(); ReviewedAt = FPlatformTime::Seconds();
    ConsentText = FText::Format(NSLOCTEXT("RidingRegistration", "Review", "탈것 등록 확인\n소비 물품: {0} × 1\n획득 탈것: {1}\n등록하면 물품 1개를 소비합니다. 서버가 현재 소유·물품 상태와 보유 여부를 최종 확인합니다."), ItemName, MountName);
    OnChanged.Broadcast(); return true;
}
bool FRPGRidingRegistrationClient::Confirm(FGuid Token)
{
    RefreshContext(); if (!Ready() || !Token.IsValid() || Token != Consent || FPlatformTime::Seconds() - ReviewedAt >= 60) { Dismiss(); return false; }
    const auto* R = Records.FindByPredicate([&](const auto& V) { return V.GetItemId() == ReviewedItem; }); const FRPGRidingRegistrationOption* Option = nullptr; FText I, M;
    if (!R || !Candidate(*R, Option) || !Names || !Names(*R, *Option, I, M) || I.ToString() != ItemName.ToString() || M.ToString() != MountName.ToString()) { Dismiss(); return false; }
    FRPGRidingIntentScope Scope; FRPGRidingPendingIntent Intent;
    const bool Valid = FRPGRidingIntentScope::Create(Bound.Endpoint, Bound.Account, Bound.Character, Scope)
        && RPGRidingCommand::Register(Scope, FGuid::NewGuid(), R->GetItemId(), R->GetRevision(), Options.CatalogVersion, Intent);
    Clear(); const bool Sent = Valid && Commands->Submit(Intent); OnChanged.Broadcast(); return Sent;
}
FRPGRidingRegistrationView FRPGRidingRegistrationClient::GetView()
{
    RefreshContext(); FRPGRidingRegistrationView V; V.bAvailable = Usable(); if (!V.bAvailable) return V;
    const auto State = Commands->GetState(); const bool Blocked = Commands->BlocksTransitions();
    V.bLoading = Phase == EPhase::Collection || Phase == EPhase::Definitions || Phase == EPhase::Inventory;
    V.bCanReload = !Blocked && !V.bLoading && FPlatformTime::Seconds() >= RetryAt && Collection->GetRetryAfterSeconds() <= 0 && Definitions->GetRetryAfterSeconds() <= 0;
    V.bCanRecover = V.bCanRetry = State == ERPGRidingCommandState::RecoveryRequired;
    V.bCanStorage = State == ERPGRidingCommandState::StorageBlocked; V.bCanRefreshAssets = Commands->NeedsFreshReads();
    const bool IsReady = Ready();
    if (!IsReady) Dismiss();
    if (IsReady)
    {
        V.Snapshot = Snapshot;
        for (const auto& R : Records)
        {
            const FRPGRidingRegistrationOption* O = nullptr; if (!Candidate(R, O)) continue; FText I, M;
            const bool Named = Names && Names(R, *O, I, M) && !I.IsEmpty() && !M.IsEmpty();
            const FText Label = Named ? FText::Format(NSLOCTEXT("RidingRegistration", "Row", "{0} × {1} → {2}"), I, FText::AsNumber(R.GetQuantity()), M)
                : NSLOCTEXT("RidingRegistration", "MissingNames", "물품·탈것 이름 정보를 확인할 수 없습니다.");
            V.Rows.Add({R.GetItemId(), FText::Format(NSLOCTEXT("RidingRegistration", "Numbered", "{0}. {1}"), V.Rows.Num() + 1, Label), Named});
        }
        V.bCanReview = V.Rows.ContainsByPredicate([](const auto& Row) { return Row.bNamed; });
    }
    V.Review = Consent; V.ReviewText = ConsentText; V.bCanConfirm = IsReady && Consent.IsValid();
    if (State == ERPGRidingCommandState::AwaitingServer) V.Status = NSLOCTEXT("RidingRegistration", "Sending", "등록 요청의 처리 결과를 확인하고 있습니다.");
    else if (V.bCanRecover) V.Status = NSLOCTEXT("RidingRegistration", "Pending", "아직 결과가 확정되지 않은 요청이 있습니다. 처리 결과 확인 또는 같은 요청 재전송을 선택해 주세요.");
    else if (V.bCanStorage) V.Status = NSLOCTEXT("RidingRegistration", "Storage", "원래 요청의 저장 상태를 확인해야 합니다. 새 등록은 보류됩니다.");
    else if (V.bCanRefreshAssets) V.Status = NSLOCTEXT("RidingRegistration", "Refreshing", "요청은 완료됐습니다. 최신 보유·가방 정보 확인이 끝나야 새 등록을 시작할 수 있습니다.");
    else if (V.bLoading) V.Status = NSLOCTEXT("RidingRegistration", "Loading", "보유 탈것·등록 조건·최신 가방을 확인하고 있습니다.");
    else if (State == ERPGRidingCommandState::Committed) V.Status = NSLOCTEXT("RidingRegistration", "Done", "등록 요청이 완료됐습니다. 물품을 다시 불러오면 현재 보유 상태를 확인할 수 있습니다.");
    else if (State == ERPGRidingCommandState::Rejected) V.Status = NSLOCTEXT("RidingRegistration", "Rejected", "서버가 등록을 허용하지 않았습니다. 최신 물품을 다시 확인해 주세요.");
    else if (Phase == EPhase::Failed) V.Status = NSLOCTEXT("RidingRegistration", "Failed", "등록 정보를 확인하지 못했습니다. 잠시 후 다시 불러와 주세요.");
    else if (IsReady && !Options.bRegistrationEnabled) V.Status = NSLOCTEXT("RidingRegistration", "Disabled", "현재 서버에서는 탈것 등록을 제공하지 않습니다.");
    else if (IsReady && V.Rows.IsEmpty()) V.Status = NSLOCTEXT("RidingRegistration", "Empty", "등록할 수 있는 물품이 없거나 이미 보유한 탈것입니다.");
    else V.Status = NSLOCTEXT("RidingRegistration", "Start", "등록할 최신 물품을 불러온 뒤 선택해 주세요. 이름 정보가 확인된 물품만 등록할 수 있습니다.");
    return V;
}
bool FRPGRidingRegistrationClient::Recover() { RefreshContext(); return Usable() && Commands->Recover(); }
bool FRPGRidingRegistrationClient::Retry() { RefreshContext(); return Usable() && Commands->Retry(); }
bool FRPGRidingRegistrationClient::RetryStorage() { RefreshContext(); return Usable() && Commands->RetryStorage(); }
bool FRPGRidingRegistrationClient::RefreshAssets() { RefreshContext(); return Usable() && Commands->NeedsFreshReads() && RefreshAction && RefreshAction(); }
