#include "Trading/RPGTradeOfferDraft.h"

bool FRPGTradeBagSnapshot::IsUsable(double Now) const
{
    if (!bReady || bLoading || !Token.IsValid() || !Character.IsValid() || Endpoint.IsEmpty() || Account.IsEmpty()
        || !FMath::IsFinite(Now) || !FMath::IsFinite(ValidUntil) || Now < 0 || Now >= ValidUntil || Items.Num() >= 500) return false;
    TSet<FGuid> Seen;
    for (const auto& Item : Items)
    {
        if (!Item.ItemId.IsValid() || Item.Revision <= 0 || Item.Revision == MAX_int64 || Item.Quantity <= 0 || Seen.Contains(Item.ItemId)) return false;
        Seen.Add(Item.ItemId);
    }
    return true;
}
const FRPGTradeBagItem* FRPGTradeBagSnapshot::Find(FGuid Id) const
{ return Items.FindByPredicate([&](const FRPGTradeBagItem& Item){ return Item.ItemId == Id; }); }
void FRPGTradeOfferDraft::Reset() { *this = FRPGTradeOfferDraft(); }
bool FRPGTradeOfferDraft::Begin(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag)
{
    if (Session.Status != ERPGTradeStatus::Negotiating || Session.CurrencyCode != TEXT("Gold") || !Actor.IsValid()
        || (Actor != Session.InitiatorCharacterId && Actor != Session.InviteeCharacterId) || Bag.Character != Actor || !Bag.Token.IsValid()
        || !Bag.bReady || Bag.bLoading || Bag.Endpoint.IsEmpty() || Bag.Account.IsEmpty()) return false;
    Base = Session; Character = Actor; BagToken = Bag.Token; Token = FGuid::NewGuid();
    Offer = Actor == Session.InitiatorCharacterId ? Session.InitiatorOffer : Session.InviteeOffer;
    Endpoint = Bag.Endpoint; Account = Bag.Account; AmountText = LexToString(Offer.CurrencyAmount); bValidAmount = true; return true;
}
bool FRPGTradeOfferDraft::IsCurrent(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag) const
{ return Token.IsValid() && Actor == Character && Bag.Character == Character && Bag.Token == BagToken && Bag.bReady && !Bag.bLoading
    && Bag.Endpoint == Endpoint && Bag.Account == Account && Session == Base; }
bool FRPGTradeOfferDraft::ParseInteger(const FString& Text, int64& Out)
{
    if (Text.IsEmpty() || Text.Len() > 19 || (Text.Len() > 1 && Text[0] == '0')) return false;
    int64 Value = 0;
    for (TCHAR C : Text)
    { if (C < '0' || C > '9' || Value > (MAX_int64-(C-'0'))/10) return false; Value = Value*10+C-'0'; }
    Out = Value; return true;
}
bool FRPGTradeOfferDraft::SetAmount(const FString& Text)
{
    if (!Token.IsValid()) return false;
    AmountText = Text.Left(32); int64 Amount;
    bValidAmount = ParseInteger(Text,Amount); if (bValidAmount) Offer.CurrencyAmount = Amount;
    return bValidAmount;
}
bool FRPGTradeOfferDraft::SetItem(FGuid Id, int32 Quantity, const FRPGTradeBagSnapshot& Bag)
{
    if (!Token.IsValid() || Bag.Token != BagToken || Quantity < 1) return false;
    const auto* Item = Bag.Find(Id);
    if (!Item || !Item->bEligible || Item->Label.IsEmpty() || Item->Quantity < Quantity) return false;
    if (auto* Existing = Offer.Items.FindByPredicate([&](const FRPGTradeItem& I){ return I.ItemId == Id; }))
    { if (Existing->ExpectedRevision != Item->Revision) return false; Existing->Quantity = Quantity; return true; }
    if (Offer.Items.Num() >= 8) return false;
    Offer.Items.Add({Id,Item->Revision,Quantity}); return true;
}
bool FRPGTradeOfferDraft::RemoveItem(FGuid Id)
{ return Token.IsValid() && Offer.Items.RemoveAll([&](const FRPGTradeItem& I){ return I.ItemId == Id; }) > 0; }
bool FRPGTradeOfferDraft::Build(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag, FRPGTradeOffer& Out) const
{
    if (!IsCurrent(Session,Actor,Bag) || !bValidAmount || !Offer.IsValid()) return false;
    for (const auto& Selected : Offer.Items)
    {
        const auto* Item = Bag.Find(Selected.ItemId);
        if (!Item || !Item->bEligible || Item->Label.IsEmpty() || Item->Revision != Selected.ExpectedRevision || Item->Quantity < Selected.Quantity) return false;
    }
    Out = Offer; return true;
}
