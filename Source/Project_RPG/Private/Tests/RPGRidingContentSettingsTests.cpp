#include "Riding/RPGRidingContentSettings.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingPolicy.h"
#include "Riding/RPGMountDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FContent
    {
        TStrongObjectPtr<URPGRidingContentSettings> Settings{NewObject<URPGRidingContentSettings>()};
        TStrongObjectPtr<URPGRidingPolicy> Policy{NewObject<URPGRidingPolicy>()};
        TStrongObjectPtr<URPGMountDefinition> Horse{NewObject<URPGMountDefinition>()};
        TStrongObjectPtr<URPGMountDefinition> Wolf{NewObject<URPGMountDefinition>()};
        FContent()
        {
            Policy->Values.GameplayHash = FString::ChrN(64, TEXT('a'));
            for (auto* D : {Horse.Get(), Wolf.Get()})
            {
                D->MountId = D == Horse.Get() ? TEXT("horse") : TEXT("wolf");
                D->DisplayName = FText::FromString(D->MountId); D->SaddleSocket = TEXT("saddle");
                D->Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/RidingSettingsFixture/Icon.Icon")));
                D->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/RidingSettingsFixture/Mesh.Mesh")));
                D->AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/RidingSettingsFixture/Anim.Anim_C")));
                D->RiderPoseLayer = D->AnimClass;
                D->DashPresentation = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/RidingSettingsFixture/Dash.Dash")));
                D->EmotePresentation = D->DashPresentation;
            }
            Settings->Policy = Policy.Get(); Settings->MountDefinitions = {Horse.Get(), Wolf.Get()};
        }
    };
    struct FProjectDefaults
    {
        URPGRidingContentSettings* Settings = GetMutableDefault<URPGRidingContentSettings>();
        TSoftObjectPtr<URPGRidingPolicy> Policy = Settings->Policy;
        TArray<TSoftObjectPtr<URPGMountDefinition>> Definitions = Settings->MountDefinitions;
        explicit FProjectDefaults(const FContent& F)
        { Settings->Policy = F.Policy.Get(); Settings->MountDefinitions = F.Settings->MountDefinitions; }
        ~FProjectDefaults() { Settings->Policy = Policy; Settings->MountDefinitions = Definitions; }
    };
    struct FActors
    {
        UWorld* World;
        ACharacter* Pawn;
        APlayerState* PlayerState;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        FActors()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            Pawn = World->SpawnActor<ACharacter>(); PlayerState = World->SpawnActor<APlayerState>();
            Riding = NewObject<URPGRidingComponent>(Pawn); Riding->RegisterComponent();
            Session = NewObject<URPGRidingSessionComponent>(PlayerState); Session->RegisterComponent();
        }
        void Begin() { Pawn->DispatchBeginPlay(); PlayerState->DispatchBeginPlay(); }
        ~FActors() { World->DestroyWorld(false); }
    };
    FObjectPropertyBase* PolicyProperty()
    {
        auto* P = FindFProperty<FObjectPropertyBase>(URPGRidingSessionComponent::StaticClass(), TEXT("Policy"));
        check(P); return P;
    }
    UObject* ReadPolicy(const FActors& F) { return PolicyProperty()->GetObjectPropertyValue_InContainer(F.Session); }
    FArrayProperty* DefinitionsProperty()
    {
        auto* P = FindFProperty<FArrayProperty>(URPGRidingComponent::StaticClass(), TEXT("Definitions"));
        check(P); return P;
    }
    FScriptArrayHelper Definitions(const FActors& F)
    {
        auto* P = DefinitionsProperty();
        return FScriptArrayHelper(P, P->ContainerPtrToValuePtr<void>(F.Riding));
    }
    UObject* Definition(const FActors& F, int32 Index)
    {
        auto Values = Definitions(F);
        return CastFieldChecked<FObjectPropertyBase>(DefinitionsProperty()->Inner)->GetObjectPropertyValue(Values.GetRawPtr(Index));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentDefaults, "ProjectRPG.Riding.Content.Defaults", Flags)
bool FRidingContentDefaults::RunTest(const FString&)
{
    TStrongObjectPtr<URPGRidingContentSettings> S{NewObject<URPGRidingContentSettings>()};
    S->Policy.Reset(); S->MountDefinitions.Reset();
    TestNull(TEXT("No policy without authored content"), S->LoadPolicy());
    TArray<TObjectPtr<URPGMountDefinition>> Out;
    TestFalse(TEXT("Empty catalog stays unconfigured"), S->LoadDefinitions(Out));
    TestTrue(TEXT("No invented defaults"), Out.IsEmpty());
    TestTrue(TEXT("Shared defaults use Game config"), S->GetClass()->HasAnyClassFlags(CLASS_Config));
    for (const auto* Name : {TEXT("Policy"), TEXT("MountDefinitions")})
    {
        const auto* P = FindFProperty<FProperty>(S->GetClass(), Name);
        TestTrue(TEXT("Content selectors persist in project config"), P && P->HasAnyPropertyFlags(CPF_Config));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentPolicy, "ProjectRPG.Riding.Content.Policy", Flags)
bool FRidingContentPolicy::RunTest(const FString&)
{
    FContent F;
    TestTrue(TEXT("Configured policy resolves to authored asset"), F.Settings->LoadPolicy() == F.Policy.Get());
    F.Policy->Values.GameplayHash.Reset();
    TestNull(TEXT("Missing gameplay hash is not a default policy"), F.Settings->LoadPolicy());
    F.Policy->Values.GameplayHash = FString::ChrN(64, TEXT('a'));
    F.Policy->Values.DashDuration = 2; F.Policy->Values.DashCooldown = 1;
    TestNull(TEXT("Invalid authored gameplay values are rejected"), F.Settings->LoadPolicy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentCatalog, "ProjectRPG.Riding.Content.Catalog", Flags)
bool FRidingContentCatalog::RunTest(const FString&)
{
    FContent F; TArray<TObjectPtr<URPGMountDefinition>> Out;
    TestTrue(TEXT("Whole valid shared catalog loads"), F.Settings->LoadDefinitions(Out));
    TestEqual(TEXT("Both metadata definitions retained"), Out.Num(), 2);
    if (Out.Num() != 2) return false;
    TestTrue(TEXT("Exact identities, without replacements"), Out[0] == F.Horse.Get() && Out[1] == F.Wolf.Get());
    TestFalse(TEXT("Reading metadata never loads cosmetic mesh"), F.Horse->Mesh.IsValid());
    TestFalse(TEXT("Reading metadata never loads cosmetic animation"), F.Horse->AnimClass.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentCatalogRejection, "ProjectRPG.Riding.Content.CatalogRejection", Flags)
bool FRidingContentCatalogRejection::RunTest(const FString&)
{
    FContent F; TArray<TObjectPtr<URPGMountDefinition>> Out{F.Horse.Get()};
    F.Settings->MountDefinitions = {F.Horse.Get(), F.Horse.Get()};
    TestFalse(TEXT("Duplicate stable identity rejected"), F.Settings->LoadDefinitions(Out));
    TestTrue(TEXT("Failure clears stale output"), Out.IsEmpty());
    F.Settings->MountDefinitions = {F.Horse.Get(), TSoftObjectPtr<URPGMountDefinition>()};
    TestFalse(TEXT("Null late entry rejects entire list"), F.Settings->LoadDefinitions(Out));
    TestTrue(TEXT("No partial catalog published"), Out.IsEmpty());
    F.Settings->MountDefinitions = {F.Horse.Get(), F.Wolf.Get()}; F.Wolf->SaddleSocket = NAME_None;
    TestFalse(TEXT("Invalid metadata rejects entire list"), F.Settings->LoadDefinitions(Out));
    F.Settings->MountDefinitions.Init(F.Horse.Get(), 513);
    TestFalse(TEXT("Catalog bound checked before loading"), F.Settings->LoadDefinitions(Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentComponents, "ProjectRPG.Riding.Content.Components", Flags)
bool FRidingContentComponents::RunTest(const FString&)
{
    FContent F; FProjectDefaults Defaults(F); FActors Actors;
    Actors.Begin();
    TestTrue(TEXT("PlayerState session reads shared policy on actual BeginPlay"), ReadPolicy(Actors) == F.Policy.Get());
    TestEqual(TEXT("Pawn reads shared definitions on actual BeginPlay"), Definitions(Actors).Num(), 2);
    if (Definitions(Actors).Num() != 2) return false;
    TestTrue(TEXT("Pawn received authored identity"), Definition(Actors, 0) == F.Horse.Get());
    TestFalse(TEXT("Loading content cannot grant owner Ready"), Actors.Session->GetOwnerState().bReady);
    TestTrue(TEXT("Loading content cannot mount without admission/ownership"), Actors.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingContentOverrides, "ProjectRPG.Riding.Content.Overrides", Flags)
bool FRidingContentOverrides::RunTest(const FString&)
{
    FContent F; FProjectDefaults Defaults(F); FActors Actors;
    TStrongObjectPtr<URPGRidingPolicy> Override{NewObject<URPGRidingPolicy>()}; // Intentionally invalid: do not conceal bad overrides.
    PolicyProperty()->SetObjectPropertyValue_InContainer(Actors.Session, Override.Get());
    auto Values = Definitions(Actors); Values.Resize(1);
    CastFieldChecked<FObjectPropertyBase>(DefinitionsProperty()->Inner)->SetObjectPropertyValue(Values.GetRawPtr(0), F.Wolf.Get());
    Actors.Begin();
    TestTrue(TEXT("Explicit policy wins even when invalid; admission validates it"), ReadPolicy(Actors) == Override.Get());
    TestEqual(TEXT("Explicit catalog is not merged/replaced"), Definitions(Actors).Num(), 1);
    TestTrue(TEXT("Explicit definition retained"), Definition(Actors, 0) == F.Wolf.Get());
    TestFalse(TEXT("Invalid explicit policy never gets Ready from defaults"), Actors.Session->GetOwnerState().bReady);
    return true;
}
#endif
