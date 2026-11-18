#include "Achievements/RPGAchievementSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    bool ReadCount(const FJsonObject& Row, const TCHAR* Field, int32& Out)
    {
        double Number;
        if (!Row.HasTypedField<EJson::Number>(Field) || !Row.TryGetNumberField(Field, Number)
            || !FMath::IsFinite(Number) || Number < 0 || Number > MAX_int32 || Number != FMath::FloorToDouble(Number)) return false;
        Out = static_cast<int32>(Number);
        return true;
    }
}

bool FRPGAchievementSnapshot::Parse(const FString& Body, FGuid Character, FRPGAchievementSnapshot& Out)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Root) || !Root) return false;
    FString Id, RosterId;
    FGuid Returned;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    FRPGAchievementSnapshot Parsed;
    if (!Root->TryGetStringField(TEXT("characterId"), Id) || !FGuid::Parse(Id, Returned) || Returned != Character
        || !Root->TryGetStringField(TEXT("rosterId"), RosterId) || !FGuid::Parse(RosterId, Parsed.Roster) || !Parsed.Roster.IsValid()
        || !Root->TryGetArrayField(TEXT("entries"), Rows) || Rows->IsEmpty() || Rows->Num() > 256) return false;
    TSet<FString> Ids;
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FRPGAchievementEntry Entry;
        if (!Value || !Value->TryGetObject(Row) || !Row || !Row->IsValid()
            || !(*Row)->TryGetStringField(TEXT("id"), Entry.Id) || Entry.Id.IsEmpty() || Ids.Contains(Entry.Id)
            || !(*Row)->TryGetStringField(TEXT("title"), Entry.Title) || Entry.Title.IsEmpty()
            || !ReadCount(**Row, TEXT("progress"), Entry.Progress) || !ReadCount(**Row, TEXT("target"), Entry.Target)
            || Entry.Target < 1 || Entry.Progress < 0 || Entry.Progress > Entry.Target) return false;
        const auto Date = (*Row)->TryGetField(TEXT("earnedAt"));
        if (Date && !Date->IsNull())
        {
            FString Iso; FDateTime ParsedDate;
            if (!Date->TryGetString(Iso) || !FDateTime::ParseIso8601(*Iso, ParsedDate)) return false;
            Entry.EarnedAt = ParsedDate;
        }
        if ((Entry.Progress == Entry.Target) != Entry.EarnedAt.IsSet()) return false;
        Ids.Add(Entry.Id);
        Parsed.Entries.Add(MoveTemp(Entry));
    }
    Out = MoveTemp(Parsed);
    return true;
}

TArray<FString> FRPGAchievementSnapshot::NewlyEarnedSince(const FRPGAchievementSnapshot& Previous) const
{
    TArray<FString> Titles;
    if (!Roster.IsValid() || Roster != Previous.Roster) return Titles;
    for (const auto& Entry : Entries)
    {
        const auto* Old = Previous.Entries.FindByPredicate([&](const auto& Row) { return Row.Id == Entry.Id; });
        if (Old && !Old->EarnedAt && Entry.EarnedAt) Titles.Add(Entry.Title);
    }
    return Titles;
}

void URPGAchievementSubsystem::OnSessionChanged(const FRPGDungeonSession& Session)
{
    if (Session.State == TEXT("SettlementPending") || Session.State == TEXT("Cleared")) NextRead = 0;
}

void URPGAchievementSubsystem::OnStoryUpdated(bool bSuccess, const FString&, bool)
{
    if (bSuccess) NextRead = 0;
}

void URPGAchievementSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (auto* Story = Collection.InitializeDependency<URPGNarrativeCoordinator>())
        Story->OnRequestFinished.AddDynamic(this, &ThisClass::OnStoryUpdated);
    if (auto* GI = GetLocalPlayer()->GetGameInstance())
        if (auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>())
            Flow->OnDungeonSessionChanged.AddDynamic(this, &ThisClass::OnSessionChanged);
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick), .5f);
}

void URPGAchievementSubsystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    if (auto* Story = GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>())
        Story->OnRequestFinished.RemoveDynamic(this, &ThisClass::OnStoryUpdated);
    if (auto* GI = GetLocalPlayer()->GetGameInstance())
        if (auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>())
            Flow->OnDungeonSessionChanged.RemoveDynamic(this, &ThisClass::OnSessionChanged);
    ResetView();
    Super::Deinitialize();
}

bool URPGAchievementSubsystem::ReadOwner(FString& OutEndpoint, FString& OutAccount, FGuid& OutCharacter) const
{
    auto* Local = GetLocalPlayer();
    auto* GI = Local ? Local->GetGameInstance() : nullptr;
    if (!GI || GI->GetFirstGamePlayer() != Local || IsRunningDedicatedServer()) return false;
    auto* Web = GI->GetSubsystem<UHttpWebManager>();
    auto* Flow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
    return Web && Flow && Web->HasAccessToken() && Web->GetPlayerEquipmentContext(OutEndpoint, OutAccount)
        && FGuid::Parse(Flow->GetSelectedCharacterId(), OutCharacter) && OutCharacter.IsValid();
}

bool URPGAchievementSubsystem::MatchesOwner() const
{
    FString Url, Steam; FGuid Selected;
    return ReadOwner(Url, Steam, Selected) && Endpoint == Url && Account == Steam && Character == Selected;
}

void URPGAchievementSubsystem::ResetView()
{
    ++Generation;
    if (Request)
    {
        Request->OnProcessRequestComplete().Unbind();
        Request->CancelRequest();
        Request.Reset();
    }
    HideNotice();
    Character.Invalidate();
    Endpoint.Reset();
    Account.Reset();
    Snapshot = {};
    bReadFailed = false;
    NextRead = 0;
}

bool URPGAchievementSubsystem::Tick(float)
{
    FString Url, Steam; FGuid Selected;
    if (!ReadOwner(Url, Steam, Selected))
    {
        if (Character.IsValid()) ResetView();
        return true;
    }
    if (Endpoint != Url || Account != Steam || Character != Selected)
    {
        ResetView();
        Endpoint = Url;
        Account = Steam;
        Character = Selected;
    }
    const double Now = FPlatformTime::Seconds();
    if (Notice && Now >= HideAt) HideNotice();
    if (!Request && Now >= NextRead) Load();
    return true;
}

void URPGAchievementSubsystem::Refresh()
{
    NextRead = 0;
    Tick(0);
}

void URPGAchievementSubsystem::Load()
{
    auto* Web = GetLocalPlayer()->GetGameInstance()->GetSubsystem<UHttpWebManager>();
    NextRead = FPlatformTime::Seconds() + 30.;
    const uint64 Expected = ++Generation;
    Request = Web->CreatePlayerNarrativeRequest(Character, FString(), [Weak = TWeakObjectPtr<ThisClass>(this), Expected](FRPGNarrativeHttpResult Result)
    {
        auto* Self = Weak.Get();
        if (!Self || Self->Generation != Expected) return;
        Self->Request.Reset();
        if (!Result.bCurrentPlayerContext || !Self->MatchesOwner()) return;
        FRPGAchievementSnapshot Next;
        Self->bReadFailed = !Result.bTransportSuccessful || Result.StatusCode != 200
            || !FRPGAchievementSnapshot::Parse(Result.Body, Self->Character, Next);
        if (Self->bReadFailed) return;
        const TArray<FString> NewTitles = Next.NewlyEarnedSince(Self->Snapshot);
        Self->Snapshot = MoveTemp(Next);
        if (!NewTitles.IsEmpty()) Self->ShowNotice(FString::Join(NewTitles, TEXT(" · ")));
    });
    if (!Request) { bReadFailed = true; return; }
    // Reuse the owner's authenticated build-only request and expiry checks for this fixed GET route.
    Request->SetURL(Endpoint / FString::Printf(TEXT("characters/%s/achievements"), *Character.ToString(EGuidFormats::DigitsWithHyphensLower)));
    const FHttpRequestPtr Started = Request;
    if (!Started->ProcessRequest() && Request == Started)
        Started->OnProcessRequestComplete().ExecuteIfBound(Started, nullptr, false);
}

FText URPGAchievementSubsystem::GetSummary() const
{
    if (!MatchesOwner()) return NSLOCTEXT("RPGAchievements", "Select", "캐릭터를 선택해 주세요.");
    if (bReadFailed) return NSLOCTEXT("RPGAchievements", "Failed", "업적을 불러오지 못했습니다. 다시 열어 재시도하세요.");
    if (!Snapshot.Roster.IsValid()) return NSLOCTEXT("RPGAchievements", "Loading", "업적을 불러오는 중…");
    TArray<FString> Lines;
    for (const auto& Entry : Snapshot.Entries)
    {
        FString Line = FString::Printf(TEXT("%s   %d/%d"), *Entry.Title, Entry.Progress, Entry.Target);
        if (Entry.EarnedAt) Line += TEXT("   달성 ") + FText::AsDate(*Entry.EarnedAt, EDateTimeStyle::Short).ToString();
        Lines.Add(MoveTemp(Line));
    }
    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void URPGAchievementSubsystem::ShowNotice(const FString& Titles)
{
    HideNotice();
    auto* Local = GetLocalPlayer();
    if (!Local || !Local->ViewportClient) return;
    Notice = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(24, 60)
        [SNew(SBorder).Padding(12)[SNew(STextBlock).Text(FText::Format(
            NSLOCTEXT("RPGAchievements", "Notice", "업적 달성 · {0}"), FText::FromString(Titles)))]];
    NoticeViewport = Local->ViewportClient;
    NoticeViewport->AddViewportWidgetForPlayer(Local, Notice.ToSharedRef(), 170);
    HideAt = FPlatformTime::Seconds() + 5.;
}

void URPGAchievementSubsystem::HideNotice()
{
    if (Notice && NoticeViewport.IsValid() && GetLocalPlayer())
        NoticeViewport->RemoveViewportWidgetForPlayer(GetLocalPlayer(), Notice.ToSharedRef());
    Notice.Reset();
    NoticeViewport.Reset();
}
