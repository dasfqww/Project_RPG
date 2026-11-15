#include "Riding/RPGRidingPreferencesClient.h"
#include "HAL/PlatformTime.h"

FRPGRidingPreferencesClient::FRPGRidingPreferencesClient(TSharedRef<FRPGRidingCollectionReader> C, TSharedRef<FRPGRidingCommandCoordinator> W,
    FRPGRidingCollectionReader::FContext Read, FNames N, TFunction<bool()> Refresh)
    : Collection(MoveTemp(C)), Commands(MoveTemp(W)), Context(MoveTemp(Read)), Names(MoveTemp(N)), RefreshAction(MoveTemp(Refresh))
{
    CollectionHandle = Collection->OnChanged.AddRaw(this, &FRPGRidingPreferencesClient::CollectionChanged);
    CommandsHandle = Commands->OnChanged.AddRaw(this, &FRPGRidingPreferencesClient::CommandsChanged);
}
FRPGRidingPreferencesClient::~FRPGRidingPreferencesClient() { Shutdown(); }
void FRPGRidingPreferencesClient::Shutdown()
{ bEnding = true; Collection->OnChanged.Remove(CollectionHandle); Commands->OnChanged.Remove(CommandsHandle); Clear(); Bound = {}; }
void FRPGRidingPreferencesClient::Clear() { Snapshot.Invalidate(); Baseline = {}; Draft = {}; Error = {}; Dismiss(); }
void FRPGRidingPreferencesClient::Dismiss() { Consent.Invalidate(); ConsentText = {}; }
void FRPGRidingPreferencesClient::RefreshContext()
{
    if (bEnding || bRefreshing || !IsInGameThread()) return; TGuardValue<bool> Guard(bRefreshing, true);
    auto C = Context ? Context() : FRPGRidingCollectionContext{}; const auto Actual = Collection->GetContext();
    C.bEligible &= Actual.bEligible && C.SameIdentity(Actual);
    if (!Bound.SameIdentity(C) || Bound.bEligible != C.bEligible) { Bound = C; Clear(); }
    Commands->RefreshContext();
}
bool FRPGRidingPreferencesClient::Available() { return !bEnding && IsInGameThread() && Bound.bEligible && Commands->GetState() != ERPGRidingCommandState::Unavailable; }
void FRPGRidingPreferencesClient::CollectionChanged()
{
    if (bEnding) return;
    Clear(); if (!bRefreshing) RefreshContext();
    const auto* C = Collection->GetCollection();
    if (!bRefreshing && Available() && !Commands->BlocksTransitions() && C && C->CharacterId == Bound.Character)
    {
        Baseline = *C; Snapshot = FGuid::NewGuid(); ReceivedAt = FPlatformTime::Seconds();
        Draft = {C->PreferencesRevision, C->SelectedMountId, C->Favorites, C->Shortcut};
    }
    OnChanged.Broadcast();
}
void FRPGRidingPreferencesClient::CommandsChanged() { if (!bEnding) { Clear(); OnChanged.Broadcast(); } }
bool FRPGRidingPreferencesClient::Editable(FGuid S)
{
    const auto* C = Collection->GetCollection();
    return Available() && S.IsValid() && S == Snapshot && !Commands->BlocksTransitions() && C && C->bPreferencesEnabled
        && C->RosterId == Baseline.RosterId && C->RosterRevision == Baseline.RosterRevision && C->PreferencesRevision == Baseline.PreferencesRevision
        && C->CatalogVersion == Baseline.CatalogVersion && C->GameplayHash == Baseline.GameplayHash
        && Baseline.PreferencesRevision < MAX_int64 && FPlatformTime::Seconds() - ReceivedAt < 60;
}
bool FRPGRidingPreferencesClient::Owned(const FString& Id, bool Empty) const
{ return (Empty && Id.IsEmpty()) || Baseline.Mounts.ContainsByPredicate([&](const auto& M) { return M.MountId == Id; }); }
bool FRPGRidingPreferencesClient::ValidDraft() const
{
    if (!Owned(Draft.SelectedMountId) || Draft.Favorites.Num() > RPGRiding::MaxFavorites) return false;
    TSet<FString> Seen; for (const auto& Id : Draft.Favorites) { if (!Owned(Id, false) || Seen.Contains(Id)) return false; Seen.Add(Id); }
    return Draft.Shortcut.Kind == ERPGRidingSelectorKind::Specific ? Owned(Draft.Shortcut.MountId, false)
        : (Draft.Shortcut.Kind == ERPGRidingSelectorKind::Selected || Draft.Shortcut.Kind == ERPGRidingSelectorKind::RandomFavorites) && Draft.Shortcut.MountId.IsEmpty();
}
bool FRPGRidingPreferencesClient::Dirty() const
{
    auto A = Draft.Favorites, B = Baseline.Favorites; A.Sort(); B.Sort();
    return Draft.SelectedMountId != Baseline.SelectedMountId || A != B || Draft.Shortcut.Kind != Baseline.Shortcut.Kind || Draft.Shortcut.MountId != Baseline.Shortcut.MountId;
}
void FRPGRidingPreferencesClient::Changed() { Dismiss(); Error = {}; OnChanged.Broadcast(); }
bool FRPGRidingPreferencesClient::Reload()
{
    RefreshContext(); if (!Available() || Commands->BlocksTransitions() || Collection->GetState() == ERPGRidingCollectionReadState::Loading || Collection->GetRetryAfterSeconds() > 0) return false;
    Clear(); return Collection->Read();
}
bool FRPGRidingPreferencesClient::Select(FGuid S, const FString& Id)
{ RefreshContext(); if (!Editable(S) || !Owned(Id)) return false; Draft.SelectedMountId = Id; Changed(); return true; }
bool FRPGRidingPreferencesClient::ToggleFavorite(FGuid S, const FString& Id)
{
    RefreshContext(); if (!Editable(S) || !Owned(Id, false)) return false;
    if (Draft.Favorites.Contains(Id)) Draft.Favorites.Remove(Id);
    else if (Draft.Favorites.Num() < RPGRiding::MaxFavorites) Draft.Favorites.Add(Id);
    else { Error = NSLOCTEXT("RidingPreferences", "FavoriteLimit", "즐겨찾기는 최대 32개까지 선택할 수 있습니다."); Dismiss(); OnChanged.Broadcast(); return false; }
    Changed(); return true;
}
bool FRPGRidingPreferencesClient::SetMode(FGuid S, ERPGRidingSelectorKind Mode)
{
    RefreshContext(); if (!Editable(S) || (Mode != ERPGRidingSelectorKind::Selected && Mode != ERPGRidingSelectorKind::Specific && Mode != ERPGRidingSelectorKind::RandomFavorites)) return false;
    if (Draft.Shortcut.Kind != Mode) Draft.Shortcut = {Mode, FString()}; Changed(); return true;
}
bool FRPGRidingPreferencesClient::SetSpecific(FGuid S, const FString& Id)
{ RefreshContext(); if (!Editable(S) || Draft.Shortcut.Kind != ERPGRidingSelectorKind::Specific || !Owned(Id)) return false; Draft.Shortcut.MountId = Id; Changed(); return true; }
bool FRPGRidingPreferencesClient::ClearAll(FGuid S)
{ RefreshContext(); if (!Editable(S)) return false; Draft = {Baseline.PreferencesRevision, FString(), {}, {ERPGRidingSelectorKind::Selected, FString()}}; Changed(); return true; }
bool FRPGRidingPreferencesClient::ResetDraft(FGuid S)
{ RefreshContext(); if (!Editable(S)) return false; Draft = {Baseline.PreferencesRevision, Baseline.SelectedMountId, Baseline.Favorites, Baseline.Shortcut}; Changed(); return true; }
bool FRPGRidingPreferencesClient::Describe(FText& Out) const
{
    auto Name = [&](const FString& Id, FText& N)
    {
        if (Id.IsEmpty()) { N = NSLOCTEXT("RidingPreferences", "None", "없음"); return true; }
        const auto* M = Baseline.Mounts.FindByPredicate([&](const auto& V) { return V.MountId == Id; });
        N = M && Names ? Names(*M) : FText::GetEmpty(); return !N.IsEmpty();
    };
    FText Selected; if (!Name(Draft.SelectedMountId, Selected)) return false;
    TArray<FString> Favorites; for (const auto& Id : Draft.Favorites) { FText N; if (!Name(Id, N)) return false; Favorites.Add(N.ToString()); }
    const FText FavoriteText = Favorites.IsEmpty() ? NSLOCTEXT("RidingPreferences", "None", "없음") : FText::FromString(FString::Join(Favorites, TEXT(", ")));
    FText Shortcut = NSLOCTEXT("RidingPreferences", "SelectedMode", "선택 탈것 호출");
    if (Draft.Shortcut.Kind == ERPGRidingSelectorKind::Specific) { FText N; if (!Name(Draft.Shortcut.MountId, N)) return false; Shortcut = FText::Format(NSLOCTEXT("RidingPreferences", "SpecificMode", "지정 탈것 호출: {0}"), N); }
    else if (Draft.Shortcut.Kind == ERPGRidingSelectorKind::RandomFavorites) Shortcut = Draft.Favorites.IsEmpty()
        ? NSLOCTEXT("RidingPreferences", "RandomEmpty", "즐겨찾기 무작위 호출 (즐겨찾기가 없어 호출할 탈것이 없습니다)") : NSLOCTEXT("RidingPreferences", "RandomMode", "즐겨찾기 무작위 호출");
    Out = FText::Format(NSLOCTEXT("RidingPreferences", "Review", "탈것 설정 저장 확인\n선택 탈것: {0}\n즐겨찾기: {1}\n빠른 호출: {2}\n위 설정을 저장합니다."), Selected, FavoriteText, Shortcut); return true;
}
bool FRPGRidingPreferencesClient::Review(FGuid S)
{
    RefreshContext(); Dismiss(); if (!Editable(S) || !ValidDraft() || !Dirty()) return false;
    if (!Describe(ConsentText)) { Error = NSLOCTEXT("RidingPreferences", "MissingName", "저장할 탈것의 이름 정보를 확인해야 합니다."); OnChanged.Broadcast(); return false; }
    Consent = FGuid::NewGuid(); Error = {}; OnChanged.Broadcast(); return true;
}
bool FRPGRidingPreferencesClient::Confirm(FGuid Token)
{
    RefreshContext(); FText Current;
    if (!Editable(Snapshot) || !ValidDraft() || !Dirty() || !Token.IsValid() || Token != Consent || !Describe(Current) || Current.ToString() != ConsentText.ToString()) { Dismiss(); return false; }
    FRPGRidingIntentScope Scope; FRPGRidingPendingIntent I;
    const bool Valid = FRPGRidingIntentScope::Create(Bound.Endpoint, Bound.Account, Bound.Character, Scope)
        && RPGRidingCommand::Preferences(Scope, FGuid::NewGuid(), Draft, I);
    Clear(); const bool Sent = Valid && Commands->Submit(I); OnChanged.Broadcast(); return Sent;
}
FRPGRidingPreferencesView FRPGRidingPreferencesClient::GetView()
{
    RefreshContext(); FRPGRidingPreferencesView V; V.bAvailable = Available(); if (!V.bAvailable) return V;
    const auto State = Commands->GetState(); V.bLoading = Collection->GetState() == ERPGRidingCollectionReadState::Loading;
    V.bCanReload = !Commands->BlocksTransitions() && !V.bLoading && Collection->GetRetryAfterSeconds() <= 0;
    V.bCanEdit = Editable(Snapshot); if (!V.bCanEdit) Dismiss();
    V.Snapshot = Snapshot; V.Draft = Draft; V.bDirty = Snapshot.IsValid() && Dirty(); V.bCanReview = V.bCanEdit && V.bDirty && ValidDraft();
    for (const auto& M : Baseline.Mounts)
    { FText N = Names ? Names(M) : FText::GetEmpty(); if (N.IsEmpty()) N = NSLOCTEXT("RidingPreferences", "Unnamed", "이름 정보를 확인할 수 없습니다");
      V.Choices.Add({M.MountId, FText::Format(NSLOCTEXT("RidingPreferences", "Choice", "{0}. {1}"), V.Choices.Num() + 1, N)}); }
    TArray<FString> Favorites; for (const auto& Id : Draft.Favorites)
    { const auto* Choice = V.Choices.FindByPredicate([&](const auto& C) { return C.Id == Id; }); if (Choice) Favorites.Add(Choice->Label.ToString()); }
    V.FavoritesText = Favorites.IsEmpty() ? NSLOCTEXT("RidingPreferences", "NoFavorites", "즐겨찾기 없음") : FText::FromString(FString::Join(Favorites, TEXT(", ")));
    V.Review = Consent; V.ReviewText = ConsentText; V.bCanConfirm = V.bCanReview && Consent.IsValid();
    V.bCanRecover = V.bCanRetry = State == ERPGRidingCommandState::RecoveryRequired; V.bCanStorage = State == ERPGRidingCommandState::StorageBlocked; V.bCanRefreshAssets = Commands->NeedsFreshReads();
    if (State == ERPGRidingCommandState::AwaitingServer) V.Status = NSLOCTEXT("RidingPreferences", "Sending", "원래 요청의 처리 결과를 확인하고 있습니다.");
    else if (V.bCanRecover) V.Status = NSLOCTEXT("RidingPreferences", "Pending", "아직 확정되지 않은 탈것 요청이 있습니다. 새 저장을 시작하지 말고 원래 결과를 확인해 주세요.");
    else if (V.bCanStorage) V.Status = NSLOCTEXT("RidingPreferences", "Storage", "원래 요청의 저장 상태를 먼저 확인해야 합니다.");
    else if (V.bCanRefreshAssets) V.Status = NSLOCTEXT("RidingPreferences", "Fresh", "저장은 확정됐습니다. 최신 보유 정보 확인을 기다리고 있습니다.");
    else if (V.bLoading) V.Status = NSLOCTEXT("RidingPreferences", "Loading", "현재 보유 탈것과 저장 설정을 불러오고 있습니다.");
    else if (!Error.IsEmpty()) V.Status = Error;
    else if (Snapshot.IsValid() && !Baseline.bPreferencesEnabled) V.Status = NSLOCTEXT("RidingPreferences", "Disabled", "현재 서버는 탈것 설정 변경을 제공하지 않습니다.");
    else if (V.bCanEdit) V.Status = NSLOCTEXT("RidingPreferences", "Editing", "변경한 내용은 검토 후 저장해야 반영됩니다. 닫거나 다시 불러오면 미저장 편집은 사라집니다.");
    else V.Status = NSLOCTEXT("RidingPreferences", "Read", "최신 보유 탈것과 설정을 불러와 주세요. 조회가 실패하거나 오래됐다면 다시 불러와야 합니다.");
    return V;
}
bool FRPGRidingPreferencesClient::Recover() { RefreshContext(); return Available() && Commands->Recover(); }
bool FRPGRidingPreferencesClient::Retry() { RefreshContext(); return Available() && Commands->Retry(); }
bool FRPGRidingPreferencesClient::RetryStorage() { RefreshContext(); return Available() && Commands->RetryStorage(); }
bool FRPGRidingPreferencesClient::RefreshAssets() { RefreshContext(); return Available() && Commands->NeedsFreshReads() && RefreshAction && RefreshAction(); }
