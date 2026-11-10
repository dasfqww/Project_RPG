#include "Trading/RPGTradeLobbyOwner.h"
#include "HAL/PlatformTime.h"

bool FRPGTradeLobbyContext::operator==(const FRPGTradeLobbyContext& O) const
{
    return Endpoint == O.Endpoint && Account == O.Account && Character == O.Character && bEnabled == O.bEnabled
        && bLocalLobby == O.bLocalLobby && bAuthenticated == O.bAuthenticated && bEligible == O.bEligible;
}
FRPGTradeLobbyOwner::FRPGTradeLobbyOwner(TSharedRef<IRPGTradeTransport> Transport, FContext InContext, TFunction<double()> InClock, FString JournalRoot)
    : Context(MoveTemp(InContext)), Clock(InClock ? MoveTemp(InClock) : TFunction<double()>([] { return FPlatformTime::Seconds(); })),
      Coordinator(MakeShared<FRPGTradeCoordinator>(Transport, Clock, MoveTemp(JournalRoot))) {}
TSharedRef<FRPGTradeLobbyOwner> FRPGTradeLobbyOwner::Create(TSharedRef<IRPGTradeTransport> Transport, FContext Context, TFunction<double()> Clock, FString JournalRoot)
{
    auto Owner = MakeShared<FRPGTradeLobbyOwner>(Transport, MoveTemp(Context), MoveTemp(Clock), MoveTemp(JournalRoot));
    TWeakPtr<FRPGTradeLobbyOwner> WeakOwner = Owner;
    Owner->Coordinator->OnChanged = [WeakOwner] { if (auto Self = WeakOwner.Pin()) Self->Changed(); };
    return Owner;
}
FRPGTradeLobbyOwner::~FRPGTradeLobbyOwner() { Shutdown(); }
void FRPGTradeLobbyOwner::Shutdown()
{
    bEnding = true; ++AssetGeneration; Assets.Reset(); Coordinator->OnChanged = {}; Coordinator->Reset(); Draft.Reset(); OnChanged.Clear();
}
void FRPGTradeLobbyOwner::Changed() { if (!bEnding) OnChanged.Broadcast(); }
bool FRPGTradeLobbyOwner::HasLiveTrade() const
{ const auto* S = GetClient().GetSession(); return Coordinator->IsRecoveryBlocked() || GetClient().GetPending() || (S && S->IsOpen()); }
bool FRPGTradeLobbyOwner::IsAvailable() const { return !bEnding && (Current.bEnabled || HasLiveTrade()); }
bool FRPGTradeLobbyOwner::BlocksTransitions() const { return !bEnding && (HasLiveTrade() || Coordinator->IsBusy()); }
bool FRPGTradeLobbyOwner::Update(bool bPoll)
{
    if (bEnding || !Context) return false;
    const auto KeepAlive = AsShared();
    const auto Next = Context();
    const bool bFrameChanged = !(Current == Next);
    if (bFrameChanged)
    { Coordinator->ClearTargets(); TargetName.Reset(); TargetInputToken=FGuid::NewGuid(); ++AssetGeneration;Assets.Reset();AssetAttemptedTrade.Invalidate();bAssetsLoading=false;bAssetsFailed=false; }
    Current = Next;
    if (Current.Account != Account || Current.Endpoint != Endpoint)
    {
        if (!Account.IsEmpty()) BlockedSelection = Current.Character;
        Coordinator->Reset(); Draft.Reset(); BoundCharacter.Invalidate();
        Account = Current.Account; Endpoint = Current.Endpoint; NextPoll = 0;
        Changed();
        if (bEnding) return false;
    }
    if (!Current.Character.IsValid()) BlockedSelection.Invalidate();
    if (Account.IsEmpty() || Endpoint.IsEmpty()) return false;
    const bool RecoveryContext=Coordinator->HasPersistence() && Current.bLocalLobby && Current.bAuthenticated;
    const bool bCanBind = ((Current.bEnabled && Current.bEligible) || RecoveryContext) && Current.Character.IsValid() && Current.Character != BlockedSelection;
    if (bCanBind && Current.Character != BoundCharacter && !HasLiveTrade() && !Coordinator->IsBusy())
    {
        Draft.Reset(); BoundCharacter = Current.Character;
        if (!Coordinator->SelectCharacter(BoundCharacter)) { BoundCharacter.Invalidate(); return false; }
        NextPoll = 0;
    }
    // A direct external selection change must not discard a previous character's uncertain write.
    Coordinator->Pump(bPoll && Current.bLocalLobby && Current.bAuthenticated);
    if (bEnding) return false;
    const bool bCanAct = BoundCharacter.IsValid() && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible;
    const double Now = Clock();
    if (bPoll && bCanAct && (Current.bEnabled || HasLiveTrade()) && !Coordinator->IsRecoveryBlocked()
        && FMath::IsFinite(Now) && Now >= NextPoll && !Coordinator->IsBusy() && !GetClient().GetPending())
    {
        const auto* S = GetClient().GetSession();
        if (!S || S->IsOpen())
        {
            NextPoll = Now + 2.0;
            if (S && S->Status == ERPGTradeStatus::Negotiating) Coordinator->RefreshPreview();
            else Coordinator->Refresh(S ? S->TradeId : FGuid());
            Changed();
        }
    }
    if (bFrameChanged) Changed();
    const auto* Completed=GetClient().GetSession();
    if(bPoll && CanReadAssets() && Completed && AssetAttemptedTrade!=Completed->TradeId) ReloadAssets();
    return bCanAct && !bEnding;
}
void FRPGTradeLobbyOwner::Tick() { Update(true); }
void FRPGTradeLobbyOwner::SetAssetSink(TFunction<bool()> Begin,TFunction<bool(const FRPGTradeAssets&)> Apply,TFunction<bool()> IsCurrent)
{ ++AssetGeneration;Assets.Reset();AssetAttemptedTrade.Invalidate();bAssetsLoading=false;bAssetsFailed=false;AssetBegin=MoveTemp(Begin);AssetApply=MoveTemp(Apply);AssetCurrent=MoveTemp(IsCurrent); }
bool FRPGTradeLobbyOwner::CanReadAssets() const
{
    const auto* S=GetClient().GetSession();
    return !bEnding && S && S->Status==ERPGTradeStatus::Completed && !GetClient().GetPending() && !Coordinator->IsBusy()
        && !bAssetsLoading && !Coordinator->IsRecoveryBlocked() && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible && Current.Character==BoundCharacter
        && bool(AssetBegin) && bool(AssetApply) && bool(AssetCurrent);
}
bool FRPGTradeLobbyOwner::ReloadAssets()
{
    if(!Update(false) || !CanReadAssets())return false;
    const auto Session=*GetClient().GetSession();const auto Scope=Current;const auto Epoch=++AssetGeneration;
    AssetAttemptedTrade=Session.TradeId;Assets.Reset();bAssetsFailed=true;
    if(!AssetBegin()){Changed();return false;}
    bAssetsLoading=true;bAssetsFailed=false;
    TWeakPtr<FRPGTradeLobbyOwner> Weak=AsShared();
    const bool Sent=Coordinator->RefreshAssets([Weak,Session,Scope,Epoch](FRPGTradeHttpResult Result)
    {
        auto Self=Weak.Pin();if(!Self || Self->bEnding)return;
        Self->Update(false);
        if(Self->AssetGeneration!=Epoch)return;
        Self->bAssetsLoading=false;Self->bAssetsFailed=true;
        const auto* CurrentSession=Self->GetClient().GetSession();FRPGTradeAssets Decoded;
        if(Self->Current==Scope && CurrentSession && *CurrentSession==Session && !Self->GetClient().GetPending()
            && Result.bCurrentPlayerContext && Result.bTransportSuccessful && Result.StatusCode==200
            && FRPGTradeAssets::Decode(Result.Body,Session,Scope.Character,Scope.Account,Decoded)
            && Self->AssetApply && Self->AssetApply(Decoded) && Self->AssetCurrent && Self->AssetCurrent())
        {Self->Assets=MoveTemp(Decoded);Self->bAssetsFailed=false;}
        Self->Changed();
    });
    if(!Sent){bAssetsLoading=false;bAssetsFailed=true;}
    Changed();return Sent;
}
bool FRPGTradeLobbyOwner::CanDiscover() const
{
    return !bEnding && !Coordinator->IsRecoveryBlocked() && Current.bEnabled && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible
        && BoundCharacter.IsValid() && Current.Character==BoundCharacter && !GetClient().GetSession() && !GetClient().GetPending();
}
FRPGTradeTargetView FRPGTradeLobbyOwner::GetTargetView() const
{
    FRPGTradeTargetView V; V.InputToken=TargetInputToken; V.Query=TargetName;
    V.bVisible=!bEnding && Current.bEnabled && !GetClient().GetSession() && !GetClient().GetPending();
    V.bCanEdit=CanDiscover() && !Coordinator->IsBusy();
    FString Name; const bool Valid=RPGTradeTargets::NormalizeName(TargetName,Name);
    V.bCanSearch=V.bCanEdit && Valid && Coordinator->CanSearchTargets();
    if (CanDiscover()) if (const auto* Results=Coordinator->GetTargets()) V.Targets=*Results;
    V.ResultToken=CanDiscover() ? Coordinator->GetTargetToken() : FGuid();
    V.bCanInvite=V.bCanEdit && V.ResultToken.IsValid() && V.Targets.Num()==1;
    const auto State=Coordinator->GetTargetState();
    if (!CanDiscover()) V.Status=NSLOCTEXT("RPGTrade","TargetContext","선택한 캐릭터로 로비에 로그인해 거래 상대를 찾으세요.");
    else if (State==ERPGTradeTargetState::Loading) V.Status=NSLOCTEXT("RPGTrade","TargetLoading","거래 상대를 찾고 있습니다.");
    else if (Valid && !Coordinator->IsBusy() && !Coordinator->CanSearchTargets()) V.Status=NSLOCTEXT("RPGTrade","TargetWait","잠시 후 검색을 다시 시도하세요.");
    else if (State==ERPGTradeTargetState::Failed || State==ERPGTradeTargetState::Throttled) V.Status=NSLOCTEXT("RPGTrade","TargetFailed","검색을 완료하지 못했습니다. 잠시 후 다시 검색하세요.");
    else if (State==ERPGTradeTargetState::Ready || State==ERPGTradeTargetState::Empty)
        V.Status=!Coordinator->GetTargets() ? NSLOCTEXT("RPGTrade","TargetExpired","검색 결과가 만료되었습니다. 다시 검색하세요.")
            : V.Targets.IsEmpty() ? NSLOCTEXT("RPGTrade","TargetEmpty","일치하는 거래 상대를 찾을 수 없습니다.")
            : NSLOCTEXT("RPGTrade","TargetChoose","상대를 선택해 초대를 보내세요. 상대의 수락이 필요합니다.");
    else V.Status=NSLOCTEXT("RPGTrade","TargetNameHint","상대 캐릭터의 정확한 이름을 입력해 검색하세요. (2~20자)");
    return V;
}
bool FRPGTradeLobbyOwner::SetTargetName(FGuid InputToken, const FString& Name)
{
    if (!Update(false) || InputToken!=TargetInputToken || !CanDiscover()) return false;
    const FString Value=Name.Left(65);
    if (Value!=TargetName) { TargetName=Value; Coordinator->ClearTargets(); Changed(); }
    return true;
}
bool FRPGTradeLobbyOwner::SearchTargets(FGuid InputToken)
{
    if (!Update(false) || InputToken!=TargetInputToken || !GetTargetView().bCanSearch) return false;
    const bool Sent=Coordinator->SearchTargets(TargetName); Changed(); return Sent;
}
bool FRPGTradeLobbyOwner::InviteTarget(FGuid ResultToken, FGuid Character)
{
    if (!Update(false)) return false;
    const auto View=GetTargetView();
    if (!View.bCanInvite || !ResultToken.IsValid() || ResultToken!=View.ResultToken
        || !View.Targets.ContainsByPredicate([Character](const FRPGTradeTarget& T){return T.Character==Character;})) return false;
    const bool Sent=Coordinator->Invite(Character); Changed(); return Sent;
}
bool FRPGTradeLobbyOwner::Refresh()
{
    if (!Update(false) || Coordinator->IsBusy()) return false;
    const auto* S = GetClient().GetSession();
    const auto* P = GetClient().GetPending();
    const bool Sent = S && S->Status == ERPGTradeStatus::Negotiating && !P ? Coordinator->RefreshPreview()
        : Coordinator->Refresh(S ? S->TradeId : P ? P->TradeId : FGuid());
    Changed(); return Sent;
}
bool FRPGTradeLobbyOwner::Retry()
{
    if (!Update(false)) return false;
    const bool Sent = Coordinator->Retry(); Changed(); return Sent;
}
bool FRPGTradeLobbyOwner::Submit(ERPGTradeAction Action)
{
    if (!Update(false) || Coordinator->IsBusy() || GetClient().GetPending()) return false;
    if (Action != ERPGTradeAction::Accept && Action != ERPGTradeAction::Cancel && Action != ERPGTradeAction::Withdraw) return false;
    if (Action == ERPGTradeAction::Accept && (!Current.bEnabled || Current.Character != BoundCharacter)) return false;
    const bool Sent = Coordinator->Submit(Action); Changed(); return Sent;
}
void FRPGTradeLobbyOwner::DismissClosed()
{
    const auto* Before=GetClient().GetSession();
    if(Before && Before->Status==ERPGTradeStatus::Completed && AssetAttemptedTrade!=Before->TradeId && CanReadAssets())ReloadAssets();
    if(bAssetsLoading || Coordinator->IsBusy() || Coordinator->IsRecoveryBlocked())return;
    const auto* S = GetClient().GetSession();
    if (S && !S->IsOpen() && Coordinator->ClearClosedSession()) { Draft.Reset(); NextPoll = 0; Changed(); }
}
bool FRPGTradeLobbyOwner::SubmitConsent(ERPGTradeAction Action, FGuid ReviewedToken)
{
    if (!Update(false) || !ReviewedToken.IsValid()) return false;
    const auto View = GetView();
    if (View.ReviewToken != ReviewedToken || !((Action == ERPGTradeAction::Ready && View.bReady)
        || (Action == ERPGTradeAction::Confirm && View.bConfirm))) return false;
    const bool Sent = Coordinator->Submit(Action); Changed(); return Sent;
}
void FRPGTradeLobbyOwner::SetBagSource(TFunction<FRPGTradeBagSnapshot()> Read, TFunction<bool()> Reload)
{ BagReader = MoveTemp(Read); BagReloader = MoveTemp(Reload); Draft.Reset(); }
bool FRPGTradeLobbyOwner::CanEditOffer() const
{
    const auto* S = GetClient().GetSession();
    return !bEnding && !Coordinator->IsRecoveryBlocked() && !bSubmittingDraft && Current.bEnabled && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible
        && BoundCharacter.IsValid() && Current.Character == BoundCharacter && S && S->Status == ERPGTradeStatus::Negotiating
        && S->CurrencyCode == TEXT("Gold") && !Coordinator->IsBusy() && !GetClient().GetPending();
}
bool FRPGTradeLobbyOwner::ReadBag(FRPGTradeBagSnapshot& Out) const
{
    if (!BagReader) return false;
    Out = BagReader(); const double Now = Clock();
    if (!FMath::IsFinite(Now) || Now < 0) return false;
    BagNow = FMath::Max(BagNow,Now);
    return Out.Endpoint == Endpoint && Out.Account == Account && Out.Character == BoundCharacter && Out.IsUsable(BagNow);
}
FRPGTradeDraftView FRPGTradeLobbyOwner::GetDraftView() const
{
    FRPGTradeDraftView V; V.Token = Draft.GetToken(); V.AmountText = Draft.GetAmountText();
    V.bCanReload = CanEditOffer() && bool(BagReloader);
    FRPGTradeBagSnapshot Bag; const bool Ready = ReadBag(Bag);
    V.bCanReload &= !Bag.bLoading;
    if (Ready) { V.BagToken = Bag.Token; V.BagItems = Bag.Items; }
    V.bCanBegin = CanEditOffer() && Ready;
    V.bCanEdit = V.bCanBegin && Draft.IsCurrent(*GetClient().GetSession(),BoundCharacter,Bag);
    FRPGTradeOffer Offer;
    V.bCanSend = V.bCanEdit && Draft.Build(*GetClient().GetSession(),BoundCharacter,Bag,Offer);
    for (const auto& Selected : Draft.GetItems())
    {
        const auto* Found = Ready ? Bag.Find(Selected.ItemId) : nullptr;
        FRPGTradeBagItem Row; Row.ItemId = Selected.ItemId; Row.Revision = Selected.ExpectedRevision; Row.Quantity = Selected.Quantity;
        Row.Label = Found && Found->Revision == Selected.ExpectedRevision ? Found->Label : NSLOCTEXT("RPGTrade", "StaleDraftItem", "가방에서 확인할 수 없는 제안 물품");
        V.SelectedItems.Add(MoveTemp(Row));
    }
    V.Status = Bag.bLoading ? NSLOCTEXT("RPGTrade", "BagLoading", "가방을 확인하고 있습니다.")
        : !Ready ? NSLOCTEXT("RPGTrade", "BagReadRequired", "최신 가방을 불러와 제안을 편집하세요.")
        : !V.Token.IsValid() ? NSLOCTEXT("RPGTrade", "BeginDraft", "현재 내 제안을 불러와 편집할 수 있습니다.")
        : !V.bCanEdit ? NSLOCTEXT("RPGTrade", "StaleDraft", "가방 또는 거래 상태가 바뀌었습니다. 현재 제안을 다시 불러오세요.")
        : !V.bCanSend ? NSLOCTEXT("RPGTrade", "InvalidDraft", "골드와 물품 수량을 확인하세요. 확인할 수 없는 물품은 제안에서 삭제한 뒤 다시 선택하세요.")
        : NSLOCTEXT("RPGTrade", "DraftReady", "수정한 제안을 보내면 양쪽의 준비와 확인이 해제됩니다.");
    return V;
}
bool FRPGTradeLobbyOwner::ReloadBag()
{ if (!Update(false) || !CanEditOffer() || !BagReloader) return false; const bool Sent = BagReloader(); Changed(); return Sent; }
bool FRPGTradeLobbyOwner::BeginDraft()
{
    FRPGTradeBagSnapshot Bag;
    if (!Update(false) || !CanEditOffer() || !ReadBag(Bag)) return false;
    const bool Began = Draft.Begin(*GetClient().GetSession(),BoundCharacter,Bag); Changed(); return Began;
}
void FRPGTradeLobbyOwner::DiscardDraft() { Draft.Reset(); Changed(); }
bool FRPGTradeLobbyOwner::SetDraftAmount(FGuid Token, const FString& Amount)
{
    if (!Update(false) || Token != Draft.GetToken() || !GetDraftView().bCanEdit) return false;
    const bool Valid = Draft.SetAmount(Amount); Changed(); return Valid;
}
bool FRPGTradeLobbyOwner::SetDraftItem(FGuid Token, FGuid Item, const FString& Quantity)
{
    FRPGTradeBagSnapshot Bag; int64 N=0;
    if (!Update(false) || Token != Draft.GetToken() || !GetDraftView().bCanEdit || !ReadBag(Bag)
        || !FRPGTradeOfferDraft::ParseInteger(Quantity,N) || N < 1 || N > MAX_int32) return false;
    const bool Set = Draft.SetItem(Item,static_cast<int32>(N),Bag); Changed(); return Set;
}
bool FRPGTradeLobbyOwner::RemoveDraftItem(FGuid Token, FGuid Item)
{ if (!Update(false) || Token != Draft.GetToken() || !GetDraftView().bCanEdit) return false; const bool Removed = Draft.RemoveItem(Item); Changed(); return Removed; }
bool FRPGTradeLobbyOwner::SendDraft(FGuid Token)
{
    FRPGTradeBagSnapshot Bag; FRPGTradeOffer Offer;
    if (!Update(false) || !CanEditOffer() || !Token.IsValid() || Token != Draft.GetToken() || !ReadBag(Bag)
        || !Draft.Build(*GetClient().GetSession(),BoundCharacter,Bag,Offer)) return false;
    bool Sent;
    { TGuardValue<bool> Guard(bSubmittingDraft,true); Sent = Coordinator->Submit(ERPGTradeAction::Offer,&Offer); }
    if (Sent) Draft.Reset();
    Changed(); return Sent;
}
FRPGTradeLobbyView FRPGTradeLobbyOwner::GetView() const
{
    FRPGTradeLobbyView V;
    const auto* S = GetClient().GetSession();
    const auto* P = GetClient().GetPending();
    const bool Eligible = !bEnding && BoundCharacter.IsValid() && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible;
    const bool RecoveryBlocked=Coordinator->IsRecoveryBlocked();
    const bool Free = Eligible && !Coordinator->IsBusy() && !P && !RecoveryBlocked;
    V.bRefresh = Eligible && !Coordinator->IsBusy() && !RecoveryBlocked; V.bRetry = Eligible && (P || RecoveryBlocked) && !Coordinator->IsBusy();
    V.bAccept = Free && Current.bEnabled && Current.Character == BoundCharacter && S && S->Status == ERPGTradeStatus::Invited && BoundCharacter == S->InviteeCharacterId;
    V.bCancel = Free && S && S->IsOpen(); V.bWithdraw = Free && S && S->Status == ERPGTradeStatus::Negotiating;
    if (!Current.bLocalLobby) V.Status = NSLOCTEXT("RPGTrade", "LobbyOnly", "로비에서 거래 상태를 확인할 수 있습니다.");
    else if (RecoveryBlocked) V.Status = NSLOCTEXT("RPGTrade", "StorageBlocked", "거래 기록을 안전하게 확인하거나 정리하지 못했습니다. 새 거래를 보내기 전에 다시 확인하세요.");
    else if (P) V.Status = NSLOCTEXT("RPGTrade", "Unknown", "거래 요청의 처리 결과를 확인하고 있습니다. 새 요청을 보내기 전에 결과를 확인하세요.");
    else if (!Current.bAuthenticated) V.Status = NSLOCTEXT("RPGTrade", "Login", "같은 계정으로 다시 로그인해 거래를 확인하세요.");
    else if (!S) V.Status = NSLOCTEXT("RPGTrade", "Empty", "진행 중인 거래가 없습니다.");
    else switch (S->Status)
    {
    case ERPGTradeStatus::Invited: V.Status = NSLOCTEXT("RPGTrade", "Invited", "거래 초대를 확인하세요. 수락 후 제안을 준비할 수 있습니다."); break;
    case ERPGTradeStatus::Negotiating: V.Status = NSLOCTEXT("RPGTrade", "Negotiating", "거래 제안을 준비 중입니다."); break;
    case ERPGTradeStatus::Completed: V.Status = NSLOCTEXT("RPGTrade", "Completed", "거래가 완료되었습니다."); break;
    case ERPGTradeStatus::Cancelled: V.Status = NSLOCTEXT("RPGTrade", "Cancelled", "거래가 취소되었습니다."); break;
    case ERPGTradeStatus::Expired: V.Status = NSLOCTEXT("RPGTrade", "Expired", "거래 시간이 만료되었습니다."); break;
    }
    if (!RecoveryBlocked && !P && Coordinator->GetLastResponse() == ERPGTradeResponse::Rejected && (!S || S->IsOpen()))
        V.Status = NSLOCTEXT("RPGTrade", "Rejected", "거래 요청을 처리할 수 없습니다. 현재 상태를 다시 확인하세요.");
    if (S)
    {
        if(S->Status==ERPGTradeStatus::Completed)
        {
            V.bReloadAssets=CanReadAssets();
            const bool Fresh=!RecoveryBlocked && Assets.IsSet() && AssetAttemptedTrade==S->TradeId && Current.bLocalLobby && Current.bAuthenticated && Current.bEligible
                && Current.Character==BoundCharacter && AssetCurrent && AssetCurrent();
            V.AssetStatus=bAssetsLoading ? NSLOCTEXT("RPGTrade","AssetsLoading","거래는 완료되었습니다. 최신 자산을 불러오고 있습니다.")
                : Fresh ? NSLOCTEXT("RPGTrade","AssetsReady","완료 후 최신 자산을 확인했습니다.")
                : NSLOCTEXT("RPGTrade","AssetsRefreshNeeded","거래는 완료되었습니다. 자산을 다시 불러와 확인하세요.");
            if(Fresh)
            {
                const auto* Gold=Assets->Wallet.Balances.FindByPredicate([](const FRPGCurrencyBalance& B){return B.CurrencyCode==FName(TEXT("Gold"));});
                const auto Coins=Gold ? FText::Format(NSLOCTEXT("RPGTrade","AssetsGold","골드 {0}"),FText::AsNumber(Gold->Balance)) : NSLOCTEXT("RPGTrade","AssetsGoldUnknown","골드 잔액 확인 불가");
                V.AssetSummary=FText::Format(NSLOCTEXT("RPGTrade","AssetSummary","{0} · 가방/장착 {1}종 · 우편 {2}종"),Coins,FText::AsNumber(Assets->Items.Num()),FText::AsNumber(Assets->Mail.Num()));
            }
        }
        auto Summary = [S](const FRPGTradeOffer& O)
        { return FText::Format(S->CurrencyCode == TEXT("Gold") ? NSLOCTEXT("RPGTrade", "GoldSummary", "물품 {0}종 · 골드 {1}")
            : NSLOCTEXT("RPGTrade", "UnknownCurrency", "물품 {0}종 · 확인할 수 없는 재화 {1}"), FText::AsNumber(O.Items.Num()), FText::AsNumber(O.CurrencyAmount)); };
        const bool First = BoundCharacter == S->InitiatorCharacterId;
        V.MyOffer = Summary(First ? S->InitiatorOffer : S->InviteeOffer);
        V.OtherOffer = Summary(First ? S->InviteeOffer : S->InitiatorOffer);
    }
    const auto* Preview = Coordinator->GetPreview();
    bool Resolved = Preview && Preview->bOpen && Preview->bAllAvailable;
    auto Render = [&](const TArray<FRPGTradePreviewItem>& Items)
    {
        TArray<FText> Lines;
        for (const auto& Item : Items)
        {
            FText Name, Description;
            const bool Known = Item.Details.IsSet() && ResolveDefinition && ResolveDefinition(Item.Details.GetValue(),Name,Description) && !Name.IsEmpty();
            Resolved &= Known;
            if (Known)
            {
                Lines.Add(FText::Format(NSLOCTEXT("RPGTrade", "ItemQuantity", "{0} × {1}"),Name,FText::AsNumber(Item.Offered.Quantity)));
                if (!Description.IsEmpty()) Lines.Add(Description);
            }
            else Lines.Add(NSLOCTEXT("RPGTrade", "UnresolvedItem", "상세를 확인할 수 없는 물품이 있습니다. 현재 내용을 다시 확인하세요."));
        }
        return FText::Join(FText::FromString(TEXT("\n")),Lines);
    };
    if (Preview && S)
    {
        const bool First = BoundCharacter == S->InitiatorCharacterId;
        V.MyDetails = Render(First ? Preview->InitiatorItems : Preview->InviteeItems);
        V.OtherDetails = Render(First ? Preview->InviteeItems : Preview->InitiatorItems);
    }
    else if (S && S->Status == ERPGTradeStatus::Negotiating)
        V.MyDetails = V.OtherDetails = NSLOCTEXT("RPGTrade", "PreviewRequired", "최신 물품 상세를 확인하고 있습니다.");
    const FGuid PreviewIdentity = Coordinator->GetPreviewToken();
    const bool CanReview = !Draft.GetToken().IsValid() && Resolved && PreviewIdentity.IsValid() && S && S->Status == ERPGTradeStatus::Negotiating && Current.bEnabled && Eligible
        && Current.Character == BoundCharacter && S->CurrencyCode == TEXT("Gold") && !P && !(S->InitiatorOffer.IsEmpty() && S->InviteeOffer.IsEmpty());
    if (CanReview)
    {
        const auto Token = PreviewIdentity;
        const FString Text = V.MyOffer.ToString()+TEXT("\n")+V.OtherOffer.ToString()+TEXT("\n")+V.MyDetails.ToString()+TEXT("\n")+V.OtherDetails.ToString();
        if (LastPreviewToken != Token || LastReviewText != Text)
        { LastPreviewToken = Token; LastReviewText = Text; DisplayReviewToken = FGuid::NewGuid(); }
        V.ReviewToken = DisplayReviewToken; V.bCanReview = !Coordinator->IsBusy();
        const bool First = BoundCharacter == S->InitiatorCharacterId;
        V.bReady = Free && !(First ? S->bInitiatorReady : S->bInviteeReady);
        V.bConfirm = Free && S->bInitiatorReady && S->bInviteeReady && !(First ? S->bInitiatorConfirmed : S->bInviteeConfirmed);
    }
    else { LastPreviewToken.Invalidate(); DisplayReviewToken.Invalidate(); LastReviewText.Reset(); }
    return V;
}
