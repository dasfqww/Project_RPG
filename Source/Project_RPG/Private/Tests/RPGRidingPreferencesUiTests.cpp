#include "UI/Riding/RPGRidingPreferencesWidget.h"
#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Riding/RPGRidingFreshRead.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingPreferencesUiTestAccess
{
    static URPGRidingPreferencesWidget* Panel(URPGRidingCollectionWidget& W) { return W.PreferencesPanel; }
    static void Register(URPGRidingCollectionWidget& W) { W.QuickSlotButton->OnClicked.Broadcast(); }
    static void Load(URPGRidingPreferencesWidget& W) { W.ReloadButton->OnClicked.Broadcast(); }
    static void Selected(URPGRidingPreferencesWidget& W, int32 I) { W.BrowseMount(W.MountIds[I-1]); W.DefaultButton->OnClicked.Broadcast(); }
    static void Favorite(URPGRidingPreferencesWidget& W, int32 I) { W.BrowseMount(W.MountIds[I]); W.FavoriteButton->OnClicked.Broadcast(); }
    static void Mode(URPGRidingPreferencesWidget& W, int32 I) { W.ModeBox->SetSelectedIndex(I); W.ModeChanged({},ESelectInfo::Direct); }
    static void Specific(URPGRidingPreferencesWidget& W, int32 I) { W.SpecificBox->SetSelectedIndex(I); W.SpecificChanged({},ESelectInfo::Direct); }
    static void Review(URPGRidingPreferencesWidget& W) { W.ReviewButton->OnClicked.Broadcast(); }
    static void Confirm(URPGRidingPreferencesWidget& W) { W.ConfirmButton->OnClicked.Broadcast(); }
    static void Clear(URPGRidingPreferencesWidget& W) { W.ClearButton->OnClicked.Broadcast(); }
    static void Reset(URPGRidingPreferencesWidget& W) { W.ResetButton->OnClicked.Broadcast(); }
    static void Recover(URPGRidingPreferencesWidget& W) { W.RecoverButton->OnClicked.Broadcast(); }
    static void Assets(URPGRidingPreferencesWidget& W) { W.AssetsButton->OnClicked.Broadcast(); }
    static FString Text(URPGRidingPreferencesWidget& W) { return W.ReviewText->GetText().ToString(); }
    static FString Saved(URPGRidingCollectionWidget& W) { return W.Settings->GetText().ToString(); }
    static void Expire(FRPGRidingPreferencesClient& C) { C.ReceivedAt -= 61; }
};
namespace
{
    using A = FRPGRidingPreferencesUiTestAccess;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& S) { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O) { FString S; FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S)); return S; }
    TSharedPtr<FJsonObject> Fixture()
    { FString S; if(!FFileHelper::LoadFileToString(S,*FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE"))))return nullptr;
      auto O=Object(S);return O && O->GetStringField(TEXT("run"))==FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN"))?O:nullptr; }
    FGuid Id(const TSharedPtr<FJsonObject>& O,const TCHAR* K) { FGuid G;FGuid::Parse(O->GetStringField(K),G);return G; }
    FString Root() { FString R;FParse::Value(FCommandLine::Get(),TEXT("RidingPreferencesRoot="),R);return R.IsEmpty()?R:R/FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    bool Ready(FAutomationTestBase& T) { if(!Fixture()||Root().IsEmpty()){T.AddError(TEXT("Private preferences fixture/root required"));return false;}return true; }
    FText Name(const FRPGRidingOwnedMount& M) { return FText::FromString(M.MountId==TEXT("horse")?TEXT("갈색 말"):TEXT("회색 늑대")); }
    FText Present(const FString& Id) { FRPGRidingOwnedMount M;M.MountId=Id;return Name(M); }
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCollectionContext* C;FCompletion Done;int32 Reads=0,Writes=0;FString Body;
        explicit FFake(FRPGRidingCollectionContext& In):C(&In){}
        virtual bool GetContext(FString& E,FString& A)const override{E=C->Endpoint;A=C->Account;return true;}
        virtual bool HasCredential()const override{return C->bEligible;}
        virtual bool CanSend()const override{return HasCredential()&&!Done;}
        virtual bool ReadCollection(FGuid,FCompletion F)override{if(!CanSend())return false;++Reads;Done=MoveTemp(F);return true;}
        virtual bool SendIntent(const FRPGRidingPendingIntent& I,FCompletion F)override{if(!CanSend())return false;++Writes;Body=I.Body;Done=MoveTemp(F);return true;}
        virtual void Cancel()override{Done={};}
        void Reply(const FString& S,int32 Code=200,double Delay=0){auto F=MoveTemp(Done);if(F)F({true,true,Code,S,Delay});}
    };
    struct FOwner
    {
        FRPGRidingCollectionContext C;TSharedPtr<FFake> CT,WT;TSharedPtr<FRPGRidingCollectionReader> Collection;
        TSharedPtr<FRPGRidingCommandCoordinator> Commands;TSharedPtr<FRPGRidingPreferencesClient> Client;
        TStrongObjectPtr<URPGRidingCollectionWidget> Parent;URPGRidingPreferencesWidget* Widget=nullptr;TSharedPtr<SWidget> Slate;
        FString Body,Journal;bool bNames=true;
        FOwner()
        {
            auto F=Fixture();C={F->GetStringField(TEXT("endpoint")),F->GetStringField(TEXT("prefsAccount")),Id(F,TEXT("prefsCharacter")),true};
            Body=F->GetStringField(TEXT("prefsBody"));Journal=Root();CT=MakeShared<FFake>(C);WT=MakeShared<FFake>(C);
            Collection=MakeShared<FRPGRidingCollectionReader>(CT.ToSharedRef(),[this]{return C;});
            Commands=MakeShared<FRPGRidingCommandCoordinator>(WT.ToSharedRef(),[this]{FRPGRidingCommandContext X;FRPGRidingIntentScope::Create(C.Endpoint,C.Account,C.Character,X.Scope);X.bEligible=C.bEligible;return X;},Journal);
            Client=MakeShared<FRPGRidingPreferencesClient>(Collection.ToSharedRef(),Commands.ToSharedRef(),[this]{return C;},[this](const auto& M){return bNames?Name(M):FText::GetEmpty();},[]{return false;});
            Parent.Reset(NewObject<URPGRidingCollectionWidget>());Parent->SetReadOwner(Collection.ToSharedRef(),Present);Parent->SetPreferencesOwner(Client.ToSharedRef());Slate=Parent->TakeWidget();Widget=A::Panel(*Parent);
        }
        ~FOwner(){Parent->CloseScreen();Client->Shutdown();Collection->Shutdown();Commands->Shutdown();Slate.Reset();Parent.Reset();}
        void Load(){A::Load(*Widget);CT->Reply(Body);}
        void Edit(){A::Selected(*Widget,1);A::Favorite(*Widget,0);A::Mode(*Widget,2);}
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiConsent,"ProjectRPG.Riding.PreferencesUI.Consent",Flags)
bool FRidingPreferencesUiConsent::RunTest(const FString&)
{
    if(!Ready(*this))return false;FOwner F;TestEqual(TEXT("Open does not read/write"),F.CT->Reads+F.WT->Writes,0);F.Load();
    TestFalse(TEXT("Programmatic widget synchronization never dirties settings"),F.Client->GetView().bDirty);F.Edit();
    TestEqual(TEXT("Edits never dispatch"),F.WT->Writes,0);A::Review(*F.Widget);TestTrue(TEXT("Human-readable summary"),A::Text(*F.Widget).Contains(TEXT("갈색 말"))&&A::Text(*F.Widget).Contains(TEXT("무작위")));
    A::Confirm(*F.Widget);A::Confirm(*F.Widget);TestEqual(TEXT("Double confirm sends one PUT"),F.WT->Writes,1);
    const auto* P=F.Commands->GetPending();TestTrue(TEXT("Settings operation and original revision"),P&&P->Operation==ERPGRidingPersistentOperation::Preferences&&Object(P->Body)->GetStringField(TEXT("expectedRevision"))==TEXT("0"));
    FRPGRidingPendingIntent Stored;TestTrue(TEXT("Persisted before response"),P&&FRPGRidingIntentJournal::Load(F.Journal,P->Scope,Stored)==ERPGRidingJournalRead::Ready&&Stored==*P);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiModes,"ProjectRPG.Riding.PreferencesUI.Modes",Flags)
bool FRidingPreferencesUiModes::RunTest(const FString&)
{
    if(!Ready(*this))return false;
    FOwner Slot;
    Slot.Load();
    A::Register(*Slot.Parent);
    TestTrue(TEXT("Register opens review for the clicked mount"), Slot.Client->GetView().bCanConfirm);
    TestEqual(TEXT("Review sends nothing until confirmed"), Slot.WT->Writes, 0);
    A::Confirm(*Slot.Widget);
    const auto Command = Object(Slot.WT->Body);
    TestTrue(TEXT("Slot saves the existing character shortcut as Specific"),
        Command && Command->GetObjectField(TEXT("shortcut"))->GetStringField(TEXT("mode")) == TEXT("Specific")
        && Command->GetObjectField(TEXT("shortcut"))->GetStringField(TEXT("mountId")) == TEXT("horse"));
    FOwner F;auto O=Object(F.Body);auto Mounts=O->GetArrayField(TEXT("mounts"));auto Wolf=Object(Json(Mounts[0]->AsObject()));Wolf->SetStringField(TEXT("mountId"),TEXT("wolf"));Wolf->SetStringField(TEXT("sourceRequestId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));Mounts.Add(MakeShared<FJsonValueObject>(Wolf));O->SetArrayField(TEXT("mounts"),Mounts);F.Body=Json(O);F.Load();
    A::Selected(*F.Widget,2);A::Mode(*F.Widget,1);TestFalse(TEXT("Specific mode requires explicit owned target"),F.Client->GetView().bCanReview);A::Specific(*F.Widget,2);A::Favorite(*F.Widget,0);A::Favorite(*F.Widget,1);A::Review(*F.Widget);
    TestTrue(TEXT("Specific target is named"),A::Text(*F.Widget).Contains(TEXT("회색 늑대")));const auto Token=F.Client->GetView().Review;A::Mode(*F.Widget,0);TestFalse(TEXT("Editing invalidates old consent"),F.Client->Confirm(Token));
    TestTrue(TEXT("Selected mode clears specific reference"),F.Client->GetView().Draft.Shortcut.MountId.IsEmpty());A::Reset(*F.Widget);TestFalse(TEXT("Reset is a local draft operation"),F.Client->GetView().bDirty);TestEqual(TEXT("Mode changes/reset send nothing"),F.WT->Writes,0);
    FOwner Empty;Empty.Load();A::Mode(*Empty.Widget,2);A::Review(*Empty.Widget);TestTrue(TEXT("Empty random set warns without selecting fallback"),A::Text(*Empty.Widget).Contains(TEXT("호출할 탈것이 없습니다")));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiClearAndLimit,"ProjectRPG.Riding.PreferencesUI.ClearAndLimit",Flags)
bool FRidingPreferencesUiClearAndLimit::RunTest(const FString&)
{
    if(!Ready(*this))return false;FOwner F;auto O=Object(F.Body);auto P=O->GetObjectField(TEXT("preferences"));P->SetStringField(TEXT("selectedMountId"),TEXT("horse"));P->SetArrayField(TEXT("favoriteMountIds"),{MakeShared<FJsonValueString>(TEXT("horse"))});P->GetObjectField(TEXT("shortcut"))->SetStringField(TEXT("mode"),TEXT("Specific"));P->GetObjectField(TEXT("shortcut"))->SetStringField(TEXT("mountId"),TEXT("horse"));O->SetStringField(TEXT("preferencesRevision"),TEXT("1"));F.Body=Json(O);F.Load();A::Clear(*F.Widget);A::Review(*F.Widget);A::Confirm(*F.Widget);
    auto Sent=Object(F.WT->Body);TestTrue(TEXT("Full clear sends null selected/empty favorites/Selected mode"),Sent&&Sent->HasTypedField<EJson::Null>(TEXT("selectedMountId"))&&Sent->GetArrayField(TEXT("favoriteMountIds")).IsEmpty()&&Sent->GetObjectField(TEXT("shortcut"))->GetStringField(TEXT("mode"))==TEXT("Selected"));
    FOwner Limit;O=Object(Limit.Body);auto Template=O->GetArrayField(TEXT("mounts"))[0]->AsObject();TArray<TSharedPtr<FJsonValue>> Many;
    for(int32 I=0;I<33;++I){auto M=Object(Json(Template));M->SetStringField(TEXT("mountId"),FString::Printf(TEXT("m%02d"),I));M->SetStringField(TEXT("sourceRequestId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));Many.Add(MakeShared<FJsonValueObject>(M));}O->SetArrayField(TEXT("mounts"),Many);Limit.Body=Json(O);Limit.Load();auto V=Limit.Client->GetView();
    for(int32 I=0;I<32;++I)TestTrue(TEXT("First 32 favorites accepted"),Limit.Client->ToggleFavorite(V.Snapshot,FString::Printf(TEXT("m%02d"),I)));
    TestFalse(TEXT("33rd favorite rejected"),Limit.Client->ToggleFavorite(V.Snapshot,TEXT("m32")));TestFalse(TEXT("Unowned mount rejected"),Limit.Client->Select(V.Snapshot,TEXT("unowned")));TestEqual(TEXT("Favorites do not auto-save"),Limit.WT->Writes,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiContext,"ProjectRPG.Riding.PreferencesUI.Context",Flags)
bool FRidingPreferencesUiContext::RunTest(const FString&)
{
    if(!Ready(*this))return false;FOwner F;F.Load();F.Edit();A::Review(*F.Widget);const auto Token=F.Client->GetView().Review;F.C.Character=FGuid::NewGuid();F.Client->RefreshContext();TestFalse(TEXT("Old character consent rejected"),F.Client->Confirm(Token));TestEqual(TEXT("Context loss sends no PUT"),F.WT->Writes,0);
    FOwner Pending;Pending.Load();Pending.Edit();A::Review(*Pending.Widget);A::Confirm(*Pending.Widget);const auto* P=Pending.Commands->GetPending();if(!P)return false;const auto Original=*P;Pending.Parent->CloseScreen();Pending.Client->Shutdown();TestTrue(TEXT("Closing keeps original settings request"),Pending.Commands->GetPending()&&*Pending.Commands->GetPending()==Original);
    Pending.Commands->Shutdown();auto Restored=MakeShared<FRPGRidingCommandCoordinator>(Pending.WT.ToSharedRef(),[&]{return FRPGRidingCommandContext{Original.Scope,true};},Pending.Journal);
    TestEqual(TEXT("Restart requires explicit recovery"),Restored->GetState(),ERPGRidingCommandState::RecoveryRequired);TestTrue(TEXT("UUID/body unchanged"),Restored->GetPending()&&*Restored->GetPending()==Original);TestEqual(TEXT("No restart retransmit"),Pending.WT->Writes,1);Restored->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiFailures,"ProjectRPG.Riding.PreferencesUI.Failures",Flags)
bool FRidingPreferencesUiFailures::RunTest(const FString&)
{
    if(!Ready(*this))return false;FOwner F;A::Load(*F.Widget);F.CT->Reply(TEXT("private detail"),503,30);TestFalse(TEXT("Read Retry-After retained"),F.Client->Reload());TestEqual(TEXT("No polling"),F.CT->Reads,1);
    FOwner Stale;Stale.Load();Stale.Edit();A::Review(*Stale.Widget);auto Token=Stale.Client->GetView().Review;A::Expire(*Stale.Client);TestFalse(TEXT("Old snapshot cannot save"),Stale.Client->Confirm(Token));TestEqual(TEXT("No stale write"),Stale.WT->Writes,0);
    FOwner NamesMissing;NamesMissing.Load();NamesMissing.Edit();A::Review(*NamesMissing.Widget);Token=NamesMissing.Client->GetView().Review;NamesMissing.bNames=false;TestFalse(TEXT("Current metadata required at confirmation"),NamesMissing.Client->Confirm(Token));
    FOwner Reload;Reload.Load();Reload.Edit();A::Review(*Reload.Widget);Token=Reload.Client->GetView().Review;Reload.Client->Reload();TestFalse(TEXT("Explicit new read invalidates consent"),Reload.Client->Confirm(Token));
    FOwner Disabled;auto O=Object(Disabled.Body);O->SetBoolField(TEXT("catalogReady"),false);O->SetBoolField(TEXT("registrationEnabled"),false);O->SetBoolField(TEXT("preferencesEnabled"),false);O->SetField(TEXT("catalogVersion"),MakeShared<FJsonValueNull>());O->SetField(TEXT("gameplayHash"),MakeShared<FJsonValueNull>());Disabled.Body=Json(O);Disabled.Load();TestFalse(TEXT("Disabled server cannot edit settings"),Disabled.Client->GetView().bCanEdit);return true;
}
namespace
{
    struct FHttp : TSharedFromThis<FHttp>
    {
        FAutomationTestBase* Test=nullptr;FRPGRidingCollectionContext C;FRPGBackendPlayerAuthState Auth;
        TSharedPtr<FRPGRidingCollectionReader> Collection;TSharedPtr<FRPGRidingCommandCoordinator> Commands;TSharedPtr<FRPGRidingPreferencesClient> Client;TSharedPtr<FRPGRidingFreshRead> Fresh;
        TStrongObjectPtr<URPGRidingCollectionWidget> Parent;URPGRidingPreferencesWidget* Widget=nullptr;TSharedPtr<SWidget> Slate;FGuid Item;int32 Quantity=-1,Stage=0;double Deadline=0,RecoverAfter=0;
        TSharedRef<FRPGHttpRidingOwnerTransport> Transport()
        {
            const TWeakPtr<FHttp> Weak=AsShared();return MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& O){auto S=Weak.Pin();if(!S)return false;O={S->C.Endpoint,S->C.Account,S->Auth.Authorization(FPlatformTime::Seconds())};return true;},
                [Weak](const FRPGRidingOwnerHttpContext& O,int32 Code){auto S=Weak.Pin();const bool Current=S&&O.Endpoint==S->C.Endpoint&&O.Account==S->C.Account&&S->Auth.MatchesRequest(O.Authorization);if(Current)S->Auth.InvalidateIfUnauthorized(O.Authorization,Code);return Current;});
        }
        void Close(){Parent->CloseScreen();Client->Shutdown();Fresh->Shutdown();Collection->Shutdown();Commands->Shutdown();Slate.Reset();Parent.Reset();}
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingPreferencesUiHttpWait,TSharedPtr<FHttp>,R);
    bool FRidingPreferencesUiHttpWait::Update()
    {
        auto& T=*R->Test;if(FPlatformTime::Seconds()>R->Deadline){T.AddError(TEXT("Preferences UMG/HTTP timeout"));R->Close();return true;}
        if(R->Stage==0&&R->Client->GetView().bCanEdit){A::Selected(*R->Widget,1);A::Favorite(*R->Widget,0);A::Mode(*R->Widget,2);A::Review(*R->Widget);T.TestTrue(TEXT("Live settings consent"),R->Client->GetView().bCanConfirm);A::Confirm(*R->Widget);A::Confirm(*R->Widget);R->Stage=1;}
        else if(R->Stage==1&&R->Commands->GetState()==ERPGRidingCommandState::RecoveryRequired){T.TestTrue(TEXT("Lost PUT keeps original request"),R->Commands->GetPending()&&R->Commands->GetPending()->Operation==ERPGRidingPersistentOperation::Preferences);R->RecoverAfter=FPlatformTime::Seconds()+1.2;R->Stage=2;}
        else if(R->Stage==2&&FPlatformTime::Seconds()>R->RecoverAfter){A::Recover(*R->Widget);R->Stage=3;}
        else if(R->Stage==3&&R->Commands->NeedsFreshReads()){A::Assets(*R->Widget);R->Stage=4;}
        else if(R->Stage==4&&R->Fresh->GetState()==ERPGRidingFreshReadState::Ready){T.TestFalse(TEXT("Fresh current assets release barrier"),R->Commands->BlocksTransitions());T.TestEqual(TEXT("Settings never consume another item"),R->Quantity,1);const auto* C=R->Collection->GetCollection();T.TestTrue(TEXT("Settings read back from server"),C&&C->PreferencesRevision==1&&C->SelectedMountId==TEXT("horse")&&C->Favorites.Num()==1&&C->Shortcut.Kind==ERPGRidingSelectorKind::RandomFavorites);T.TestTrue(TEXT("Open parent shows actual saved selection"),A::Saved(*R->Parent).Contains(TEXT("갈색 말")));R->Close();return true;}
        return false;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingPreferencesUiMemoryHttp,"ProjectRPG.Riding.PreferencesUI.MemoryHttp",Flags)
bool FRidingPreferencesUiMemoryHttp::RunTest(const FString&)
{
    if(!Ready(*this))return false;auto F=Fixture();auto R=MakeShared<FHttp>();R->Test=this;R->C={F->GetStringField(TEXT("endpoint")),F->GetStringField(TEXT("prefsAccount")),Id(F,TEXT("prefsCharacter")),true};R->Auth.SetOpaqueToken(F->GetStringField(TEXT("prefsToken")));R->Item=Id(F,TEXT("prefsItem"));const TWeakPtr<FHttp> Weak=R;
    auto Context=[Weak]{auto S=Weak.Pin();return S?S->C:FRPGRidingCollectionContext{};};auto CommandContext=[Weak]{auto S=Weak.Pin();FRPGRidingCommandContext C;if(S){FRPGRidingIntentScope::Create(S->C.Endpoint,S->C.Account,S->C.Character,C.Scope);C.bEligible=S->C.bEligible;}return C;};
    R->Collection=MakeShared<FRPGRidingCollectionReader>(R->Transport(),Context);R->Commands=MakeShared<FRPGRidingCommandCoordinator>(R->Transport(),CommandContext,Root());
    FRPGRidingFreshRead::FSink Sink{[](FGuid){return true;},[]{return true;},[Weak](const auto& Items,const auto&){auto S=Weak.Pin();if(!S)return false;S->Quantity=0;for(const auto& I:Items)if(I.GetItemId()==S->Item)S->Quantity=I.GetQuantity();return true;},[Weak,Context](const auto& C){auto S=Weak.Pin();return S&&S->Collection->InstallFreshSnapshot(Context(),C);}};
    R->Fresh=MakeShared<FRPGRidingFreshRead>(R->Commands.ToSharedRef(),R->Transport(),CommandContext,MoveTemp(Sink));R->Client=MakeShared<FRPGRidingPreferencesClient>(R->Collection.ToSharedRef(),R->Commands.ToSharedRef(),Context,Name,[Weak]{auto S=Weak.Pin();return S&&S->Fresh->Refresh();});
    R->Parent.Reset(NewObject<URPGRidingCollectionWidget>());R->Parent->SetReadOwner(R->Collection.ToSharedRef(),Present);R->Parent->SetPreferencesOwner(R->Client.ToSharedRef());R->Slate=R->Parent->TakeWidget();R->Widget=A::Panel(*R->Parent);A::Load(*R->Widget);R->Deadline=FPlatformTime::Seconds()+25;ADD_LATENT_AUTOMATION_COMMAND(FRidingPreferencesUiHttpWait(R));return true;
}
#endif
