#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Identity/RPGGreatSwordIdentityWidget.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGGreatSwordIdentityWidgetTestAccess
{
    static void Attach(URPGGreatSwordIdentityWidget& W) { W.bAutoBindOwningPawn=false; W.NativeConstruct(); }
    static void Detach(URPGGreatSwordIdentityWidget& W) { W.NativeDestruct(); W.ReleaseSlateResources(true); }
    static UUserWidget* Skin(URPGGreatSwordIdentityWidget& W) { return W.WBP_IdentityGaugeBar; }
};
namespace
{
struct FIdentityWidgetFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
    UWorld* World=nullptr;
    URPGGreatSwordIdentityWidget* Widget=nullptr;
    TSharedPtr<SWidget> Slate;
    bool Initialize(bool bAuthored=false)
    {
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/IdentityHUD_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
        World=Instance->GetWorld();
        if (!World) return false;
        World->InitializeActorsForPlay(FURL());
        UClass* Type=bAuthored ? LoadClass<URPGGreatSwordIdentityWidget>(nullptr,TEXT("/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerIdentity.WBP_PlayerIdentity_C")) : URPGGreatSwordIdentityWidget::StaticClass();
        if (!Type) return false;
        Widget=NewObject<URPGGreatSwordIdentityWidget>(World,Type);
        return Widget && Widget->Initialize();
    }
    ARPGPlayer* Source(bool bUnlocked=true)
    {
        auto* P=World->SpawnActor<ARPGPlayer>();
        auto* ASC=P->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(P,P);
        ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100.f);
        auto* D=LoadObject<URPGPlayerClassDefinition>(nullptr,TEXT("/Game/Blueprints/GameData/PlayerClass/DA_Class_GreatSwordWarrior.DA_Class_GreatSwordWarrior"));
        if (!D) return nullptr;
        FRPGPlayerSkillRestoreData Data; Data.ClassId=D->GetPrimaryAssetId(); Data.bIdentityUnlocked=bUnlocked;
        auto* Growth=P->GetPlayerSkillComponent(); const auto G=Growth->BeginClassSkillRestore(Data.ClassId); FText Error;
        if (!Growth->CompleteClassSkillRestore(G,D,Data,Error)) return nullptr;
        return P;
    }
    ~FIdentityWidgetFixture()
    {
        if (Widget) FRPGGreatSwordIdentityWidgetTestAccess::Detach(*Widget);
        Slate.Reset(); Instance->Shutdown(); RPGTestWorld::Destroy(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdentityHUDStateTest,"ProjectRPG.UI.GreatSwordIdentity.StateAndLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIdentityHUDStateTest::RunTest(const FString&)
{
    FIdentityWidgetFixture F;
    if (!TestTrue(TEXT("Fixture"),F.Initialize())) return false;
    auto* P=F.Source(); auto* Q=F.Source(false);
    if (!TestNotNull(TEXT("Restored source"),P) || !TestNotNull(TEXT("Locked source"),Q)) return false;
    auto* ASC=P->GetRPGAbilitySystemComponent();
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),100.f);
    F.Widget->SetIdentitySource(ASC); FRPGGreatSwordIdentityWidgetTestAccess::Attach(*F.Widget);
    TestEqual(TEXT("Reads values that arrived before Construct"),F.Widget->GetDisplayedIdentity().Gauge,100.f);
    TestFalse(TEXT("Full gauge alone is not berserk"),F.Widget->GetDisplayedIdentity().bBerserk);
    auto* Model=F.Widget->GetIdentityViewModel();
    if (!TestNotNull(TEXT("Identity state has its own MVVM owner"),Model)) return false;
    TestTrue(TEXT("ASC subscribes the ViewModel, not the View"),
        ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).IsBoundToObject(Model));
    TestTrue(TEXT("Full label"),F.Widget->GetIdentityText().ToString().Contains(TEXT("충전 완료")));
    const auto Ready=ASC->ApplyGameplayEffectToSelf(GetDefault<URPGGreatSwordResolveReadyEffect>(),1.f,ASC->MakeEffectContext());
    TestTrue(TEXT("Mode changes even without gauge movement"),F.Widget->GetDisplayedIdentity().bBerserk);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),50.f);
    TestEqual(TEXT("Half gauge"),F.Widget->GetDisplayedIdentity().Fraction,.5f);
    TestTrue(TEXT("Half gauge remains berserk"),F.Widget->GetIdentityText().ToString().Contains(TEXT("폭주")));
    ASC->RemoveActiveGameplayEffect(Ready);
    TestFalse(TEXT("Mode removal updates unchanged half gauge"),F.Widget->GetDisplayedIdentity().bBerserk);
    F.Widget->SetIdentitySource(Q->GetRPGAbilitySystemComponent());
    TestFalse(TEXT("Old source releases its ViewModel subscription"),ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).IsBoundToObject(Model));
    TestFalse(TEXT("Old source unbound"),ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).IsBoundToObject(F.Widget));
    TestFalse(TEXT("Locked source"),F.Widget->GetDisplayedIdentity().bUnlocked);
    TestTrue(TEXT("Locked label"),F.Widget->GetIdentityText().ToString().Contains(TEXT("미해금")));
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),25.f);
    TestFalse(TEXT("Old source cannot change new view"),F.Widget->GetDisplayedIdentity().bUnlocked);
    F.Widget->SetIdentitySource(ASC);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0.f);
    TestFalse(TEXT("Death clears presentation"),F.Widget->GetDisplayedIdentity().bHasSource);
    FRPGGreatSwordIdentityWidgetTestAccess::Detach(*F.Widget);
    TestFalse(TEXT("Destruct removes mode listener"),ASC->RegisterGameplayTagEvent(RPGGameplayTags::Player_Status_Identity_GreatSword_Ready,EGameplayTagEventType::NewOrRemoved).IsBoundToObject(F.Widget));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdentityHUDSkinTest,"ProjectRPG.UI.GreatSwordIdentity.AuthoredSkin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIdentityHUDSkinTest::RunTest(const FString&)
{
    FIdentityWidgetFixture F;
    if (!TestTrue(TEXT("Authored module"),F.Initialize(true))) return false;
    auto* P=F.Source(); if (!TestNotNull(TEXT("Source"),P)) return false;
    auto* ASC=P->GetRPGAbilitySystemComponent();
    F.Widget->SetIdentitySource(ASC); F.Slate=F.Widget->TakeWidget();
    auto* Skin=FRPGGreatSwordIdentityWidgetTestAccess::Skin(*F.Widget);
    if (!TestNotNull(TEXT("Authored status template"),Skin)) return false;
    auto* Bar=Cast<UProgressBar>(Skin->GetWidgetFromName(TEXT("ProgressBar_Main")));
    auto* Text=Cast<UTextBlock>(Skin->GetWidgetFromName(TEXT("ProgressText")));
    if (!TestNotNull(TEXT("Authored bar"),Bar) || !TestNotNull(TEXT("Authored label"),Text)) return false;
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),50.f);
    TestEqual(TEXT("Actual primitive receives replicated-attribute delegate"),Bar->GetPercent(),.5f);
    TestTrue(TEXT("Actual label receives gauge"),Text->GetText().ToString().Contains(TEXT("50/100")));
    ASC->ApplyGameplayEffectToSelf(GetDefault<URPGGreatSwordResolveReadyEffect>(),1.f,ASC->MakeEffectContext());
    TestTrue(TEXT("Actual label receives mode"),Text->GetText().ToString().Contains(TEXT("폭주")));
    TestTrue(TEXT("Template cannot hide the identity label"),Text->IsVisible());
    TestEqual(TEXT("No duration label for an untimed mode"),Skin->GetWidgetFromName(TEXT("ProgressTimeText"))->GetVisibility(),ESlateVisibility::Collapsed);
    TestEqual(TEXT("UI cannot spend gauge"),ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),50.f);
    TestEqual(TEXT("The old rectangular meter does not double-paint the circle"),Skin->GetRenderOpacity(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdentityMVVMNotificationTest,"ProjectRPG.UI.GreatSwordIdentity.ViewModelNotifications",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIdentityMVVMNotificationTest::RunTest(const FString&)
{
    FIdentityWidgetFixture F;
    if (!TestTrue(TEXT("Fixture"),F.Initialize())) return false;
    auto* P=F.Source(); if (!TestNotNull(TEXT("Source"),P)) return false;
    auto* ASC=P->GetRPGAbilitySystemComponent();
    TStrongObjectPtr<URPGGreatSwordIdentityViewModel> Model(NewObject<URPGGreatSwordIdentityViewModel>());
    Model->SetSource(ASC); Model->Activate();
    int32 Notifications=0;
    bool bCoherent=true;
    const auto Handle=Model->AddFieldValueChangedDelegate(
        URPGGreatSwordIdentityViewModel::FFieldNotificationClassDescriptor::GaugePercent,
        INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*,UE::FieldNotification::FFieldId)
        {
            ++Notifications;
            bCoherent &= Model->Displayed.Fraction==Model->GaugePercent && !Model->PercentText.IsEmpty();
        }));
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),25.f);
    TestEqual(TEXT("Quarter circle"),Model->GaugePercent,.25f);
    TestTrue(TEXT("Quarter percentage is readable"),Model->PercentText.ToString().Contains(TEXT("25")));
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),100.f);
    TestTrue(TEXT("Full is labelled as charged rather than a promise of server activation"),Model->ModeText.ToString().Contains(TEXT("충전 완료")));
    TestEqual(TEXT("Attribute events update FieldNotify"),Notifications,2);
    TestTrue(TEXT("Bound properties form one coherent snapshot before notifications"),bCoherent);
    Model->RemoveFieldValueChangedDelegate(URPGGreatSwordIdentityViewModel::FFieldNotificationClassDescriptor::GaugePercent,Handle);
    const float Before=ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute());
    Model->Deactivate();
    TestFalse(TEXT("Deactivation clears old identity"),Model->Displayed.bHasSource);
    TestEqual(TEXT("Deactivation clears fill"),Model->GaugePercent,0.f);
    TestTrue(TEXT("Deactivation clears mode"),Model->ModeText.IsEmpty());
    TestFalse(TEXT("No delegate retained after deactivation"),ASC->GetGameplayAttributeValueChangeDelegate(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()).IsBoundToObject(Model.Get()));
    TestEqual(TEXT("ViewModel never writes the resource"),ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),Before);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),40.f);
    TestEqual(TEXT("Old source cannot repaint deactivated model"),Model->GaugePercent,0.f);
    Model->SetSource(ASC); Model->Activate();
    TestEqual(TEXT("Reactivation reads a value already replicated"),Model->GaugePercent,.4f);
    P->Destroy();
    TestFalse(TEXT("Avatar destruction clears state without a tick"),Model->Displayed.bHasSource);
    Model->Deactivate();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIdentityContextVisibilityTest,"ProjectRPG.UI.GreatSwordIdentity.ContextVisibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIdentityContextVisibilityTest::RunTest(const FString&)
{
    FIdentityWidgetFixture F;
    if (!TestTrue(TEXT("Fixture"),F.Initialize())) return false;
    auto* P=F.Source(); if (!TestNotNull(TEXT("Source"),P)) return false;
    auto* ASC=P->GetRPGAbilitySystemComponent();
    F.Widget->SetIdentitySource(ASC); FRPGGreatSwordIdentityWidgetTestAccess::Attach(*F.Widget);
    F.Widget->SetVisibility(ESlateVisibility::Collapsed);
    ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentIdentityGaugeAttribute(),50.f);
    TestEqual(TEXT("Gauge events do not undo a mounted presenter's hide"),F.Widget->GetVisibility(),ESlateVisibility::Collapsed);
    F.Widget->SetIdentitySource(nullptr); F.Widget->SetIdentitySource(ASC);
    TestEqual(TEXT("Source recovery also respects external hiding"),F.Widget->GetVisibility(),ESlateVisibility::Collapsed);
    F.Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    TestEqual(TEXT("Restored center already holds the latest resource"),F.Widget->GetIdentityViewModel()->GaugePercent,.5f);
    return true;
}
#endif
