#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Manager/DataManager.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGEquipmentDamageTests
{
struct FScope
{
	UWorld* World = nullptr;
	~FScope() { if(World) RPGTestWorld::Destroy(World); }
	ARPGSkillDamageSecurityTestPawn* Pawn(FVector Location,uint8 Team)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* A=World->SpawnActor<ARPGSkillDamageSecurityTestPawn>(Location,FRotator::ZeroRotator,P);
		auto* C=World->SpawnActor<AAIController>();
		if(!A||!C) return nullptr;
		C->SetGenericTeamId(FGenericTeamId(Team)); C->Possess(A); return A;
	}
};
FRPGItemRecord Record(UDataManager& Data,const FRPGItemOwnerRef& Owner,const TCHAR* Name,EEquipmentSlotType Slot,int32 Level,FGuid Item=FGuid(),int64 Revision=1)
{
	FRPGItemRecord R;
	const FPrimaryAssetId Id(TEXT("RPGItemDefinition"),FName(FString(TEXT("GameItem.Equipment.Progression.Target."))+Name));
	const auto* D=Data.FindNativeItemDefinition(Id); if(!D) return R;
	FRPGEquipmentProgressionState G; G.GrowthBandId=TEXT("FirstSlice"); G.RuleVersion=1; G.Level=Level;
	FRPGItemInstanceState S; if(!FRPGItemInstanceState::TryRestore(Item.IsValid()?Item:FGuid::NewGuid(),0,1,{}, {},S,G)) return R;
	FRPGItemLocation L; L.ContainerType=ERPGItemContainerType::Equipment; L.ContainerId=Owner.OwnerId; L.SlotIndex=static_cast<int32>(Slot);
	FRPGItemRecordMetadata M; M.BindState=ERPGItemBindState::CharacterBound;
	FRPGItemRecord::TryRestore(Id,1,Owner,L,S,Revision,ERPGItemLifecycleState::Active,M,R); return R;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentApprovedDamageTest,"ProjectRPG.Item.EquipmentProgression.ApprovedDamageAndFinalCap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentApprovedDamageTest::RunTest(const FString&)
{
	using namespace RPGEquipmentDamageTests;
	FScope F;
	F.World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));
	if(!TestNotNull(TEXT("World"),F.World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(F.World);
	F.World->InitializeActorsForPlay(FURL()); F.World->BeginPlay(); F.World->GetWorldSettings()->NotifyBeginPlay();
	auto* Source=F.Pawn(FVector::ZeroVector,1); auto* Target=F.Pawn(FVector(200,0,0),2);
	if(!TestNotNull(TEXT("Source"),Source)||!TestNotNull(TEXT("Target"),Target)) return false;
	Source->Attributes()->SetAttack(100.f); Target->Attributes()->SetDefense(10.f);
	auto* Equipment=NewObject<URPGAuthoritativeEquipmentComponent>(Source);
	Source->AddInstanceComponent(Equipment); Equipment->RegisterComponent();
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UDataManager> Data(NewObject<UDataManager>(Instance.Get())); Data->RefreshItemCache();
	FRPGItemOwnerRef Owner; Owner.Type=ERPGItemOwnerType::Character; Owner.OwnerId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	const auto Handle=Source->ASC()->GiveAbility(FGameplayAbilitySpec(URPGSkillDamageSecurityTestAbility::StaticClass(),1));
	const auto* AbilitySpec=Source->ASC()->FindAbilitySpecFromHandle(Handle);
	auto* Ability=AbilitySpec?Cast<URPGSkillDamageSecurityTestAbility>(AbilitySpec->GetPrimaryInstance()):nullptr;
	if(!TestNotNull(TEXT("Approved skill ability"),Ability)) return false;
	auto Hit=[&](float Expected,float FinalCap=500.f)
	{
		if(Ability->IsActive()) Ability->Finish();
		FRPGSkillRuntimeSpec Spec; Spec.SkillTag=RPGGameplayTags::Player_Ability_Skill_AssultBlade;
		Spec.ExecutionPolicyClass=URPGSkillExecutionPolicy::StaticClass(); Spec.BaseDamage=10.f;
		Spec.TargetingProfile.Shape.Type=ERPGHitQueryShape::Sector; Spec.TargetingProfile.Shape.Radius=500.f;
		Spec.TargetingProfile.Shape.AngleDegrees=90.f; Spec.TargetingProfile.Shape.HalfHeight=100.f;
		Spec.TargetingProfile.Filter.TeamRule=ERPGHitQueryTeamRule::HostileOnly;
		Spec.SecurityProfile.MaximumServerHitDistance=600.f; Spec.SecurityProfile.MaximumTargetsPerQuery=8;
		Spec.SecurityProfile.MaximumHitsPerActivation=1; Spec.SecurityProfile.MaximumDamagePerHit=100.f;
		Spec.SecurityProfile.MaximumFinalDamagePerHit=FinalCap;
		Ability->Configure(Spec,RPGGameplayTags::Player_Event_AOE);
		F.World->Tick(LEVELTICK_All,.01f);
		if(!TestTrue(TEXT("Activate authoritative skill"),Source->ASC()->TryActivateAbility(Handle))) return;
		const float Before=Target->Attributes()->GetCurrentHealth();
		TArray<FRPGHitQueryResult> Hits; int32 Count=0; FText Error;
		const bool Applied=Ability->ExecuteAuthorizedSkillDamage(URPGSkillDamageSecurityTestEffect::StaticClass(),10.f,FGameplayTag(),Hits,Count,Error);
		TestEqual(TEXT("Final damage admission"),Applied,Expected>0.f);
		TestEqual(TEXT("Actual target HP delta"),Before-Target->Attributes()->GetCurrentHealth(),Expected);
		TestEqual(TEXT("Approved hit count"),Count,Expected>0.f?1:0);
		TestEqual(TEXT("Activation budget consumed only by approved damage"),Ability->HitCount(),Expected>0.f?1:0);
		UE_LOG(LogTemp,Display,TEXT("RPG_EQUIPMENT_APPROVED_DAMAGE Attack=%.0f Expected=%.0f Before=%.0f After=%.0f FinalCap=%.0f Approved=%d"),Source->Attributes()->GetAttack(),Expected,Before,Target->Attributes()->GetCurrentHealth(),FinalCap,Count);
	};
	Hit(100.f);
	auto Sword=Record(*Data,Owner,TEXT("EnduranceGreatSword"),EEquipmentSlotType::Weapon_Primary_R,0);
	auto Gloves=Record(*Data,Owner,TEXT("Gloves"),EEquipmentSlotType::Hands,0);
	TestTrue(TEXT("Equip target +0"),Equipment->Reconcile(Owner,{Sword,Gloves},*Data));
	Hit(114.f);
	TestTrue(TEXT("Repeat same saved records"),Equipment->Reconcile(Owner,{Sword,Gloves},*Data)); Hit(114.f);
	Sword=Record(*Data,Owner,TEXT("EnduranceGreatSword"),EEquipmentSlotType::Weapon_Primary_R,3,Sword.GetItemId(),2);
	Gloves=Record(*Data,Owner,TEXT("Gloves"),EEquipmentSlotType::Hands,3,Gloves.GetItemId(),2);
	TestTrue(TEXT("Restore newer stages"),Equipment->Reconcile(Owner,{Sword,Gloves},*Data)); Hit(129.f);
	// The approved base magnitude is legal; its gear-amplified final damage is not.
	Hit(0.f,120.f);
	Equipment->SetActiveEquipState(EEquipState::WeaponSet_Secondary); Hit(110.f);
	Equipment->SetActiveEquipState(EEquipState::WeaponSet_Primary); Hit(129.f);
	TestTrue(TEXT("Unequip all"),Equipment->Reconcile(Owner,{},*Data)); Hit(100.f);
	Ability->Finish();
	return true;
}
#endif
