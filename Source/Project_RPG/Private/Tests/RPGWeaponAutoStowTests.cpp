#if WITH_DEV_AUTOMATION_TESTS

#include "Ability/RPGComboSkillAbility.h"
#include "Ability/Player/RPGGameplayAbility_WeaponStance.h"
#include "Animation/AnimMontage.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGGameplayTags.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWeaponAutoStowTest,
	"ProjectRPG.Item.WeaponLoadout.AutomaticAttackDrawAndTenSecondStow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGWeaponAutoStowTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); };
	auto* Avatar = World->SpawnActor<ACharacter>();
	auto* Mesh = NewObject<USkeletalMesh>();
	for (const FName Name : { FName(TEXT("WeaponHandSocket")), FName(TEXT("WeaponBackSocket")) })
	{
		auto* Socket = NewObject<USkeletalMeshSocket>(Mesh);
		Socket->SocketName = Name;
		Socket->BoneName = TEXT("root"); // DoesSocketExist requires a named socket bone.
		Mesh->GetMeshOnlySocketList().Add(Socket);
	}
	Avatar->GetMesh()->SetSkeletalMesh(Mesh);
	auto* ASC = NewObject<URPGAbilitySystemComponent>(Avatar);
	Avatar->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Avatar, Avatar);
	auto* Combat = NewObject<UPlayerCombatComponent>(Avatar);
	Avatar->AddInstanceComponent(Combat);
	Combat->RegisterComponent();
	FActorSpawnParameters Params;
	Params.Owner = Avatar;
	auto* Weapon = World->SpawnActor<ARPGPlayerWeapon>(Params);
	FRPGPlayerAbilitySet Attack;
	Attack.InputTag = RPGGameplayTags::InputTag_LightAttack_GreatSword;
	Attack.AbilityToGrant = URPGComboSkillAbility::StaticClass();
	Weapon->PlayerWeaponData.DefaultWeaponAbilities = { Attack };
	Combat->RegisterSpawnedWeapon(RPGGameplayTags::Player_Weapon_GreatSword, Weapon, false);
	FGameplayAbilitySpec LevelSource(URPGGameplayAbility_WeaponStance::StaticClass(), 3);
	LevelSource.SourceObject = Avatar;
	LevelSource.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_EquipWeapon);
	const auto EquipHandle = ASC->GiveAbility(LevelSource);
	auto* Loadout = NewObject<URPGPlayerWeaponLoadoutComponent>(Avatar);
	Avatar->AddInstanceComponent(Loadout);
	Loadout->RegisterComponent();
	TestEqual(TEXT("Startup weapon immediately has its attack loadout"), Loadout->GrantedHandles.Num(), 1);
	TestEqual(TEXT("Initial visible carry is the back socket"), Weapon->GetAttachParentSocketName(), FName(TEXT("WeaponBackSocket")));
	TestEqual(TEXT("Sheathed weapon is still selected for attack queries"), Combat->GetPlayerCurrentEquippedWeapon(), Weapon);
	TestFalse(TEXT("Equip command cannot activate"), ASC->TryActivateAbility(EquipHandle, false));
	ASC->OnAbilityInputPressed(RPGGameplayTags::InputTag_EquipWeapon);
	TestFalse(TEXT("Retired command is not pressed"), ASC->IsAbilitySpecInputPressed(EquipHandle));
	const auto Handles = Loadout->GrantedHandles;
	uint64 Frame = GFrameCounter;
	const auto Advance = [&](const float Seconds)
	{
		// Prime newly scheduled timers, then advance on a distinct simulated engine frame.
		for (const float Delta : { 0.f, Seconds })
		{
			TGuardValue<uint64> FrameGuard(GFrameCounter, ++Frame);
			World->GetTimerManager().Tick(Delta);
		}
	};
	const auto* AttackCDO = GetDefault<URPGComboSkillAbility>();
	Loadout->NotifyAttackStarted(AttackCDO);
	TestEqual(TEXT("First attack immediately attaches to hand"), Weapon->GetAttachParentSocketName(), FName(TEXT("WeaponHandSocket")));
	Advance(9.f);
	TestEqual(TEXT("Still drawn before ten seconds"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Drawn);
	Loadout->NotifyAttackStarted(AttackCDO);
	Advance(9.f);
	TestEqual(TEXT("A new attack restarts the full idle interval"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Drawn);
	Advance(1.1f);
	TestEqual(TEXT("Headless completion fallback stows after ten seconds"), Weapon->GetAttachParentSocketName(), FName(TEXT("WeaponBackSocket")));
	TestTrue(TEXT("Stow keeps the same input abilities and handles"), Loadout->GrantedHandles == Handles);
	Loadout->NotifyAttackStarted(AttackCDO);
	ASC->FindAbilitySpecFromHandle(Handles[0])->ActiveCount = 1;
	Advance(10.1f);
	TestEqual(TEXT("An active long attack defers auto stow"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Drawn);
	ASC->FindAbilitySpecFromHandle(Handles[0])->ActiveCount = 0;
	Advance(0.3f);
	TestEqual(TEXT("Idle stow resumes after long attack finishes"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Stowed);
	Loadout->NotifyAttackStarted(AttackCDO);
	Loadout->Presentation.CarryState = ERPGWeaponCarryState::Sheathing;
	Loadout->PlayingStowMontage = NewObject<UAnimMontage>();
	FGameplayEventData Payload;
	ASC->HandleGameplayEvent(FGameplayTag::RequestGameplayTag(TEXT("Player.Event.Unequip.GreatSword")), &Payload);
	TestEqual(TEXT("Authored notify stows during animation"), Weapon->GetAttachParentSocketName(), FName(TEXT("WeaponBackSocket")));
	Loadout->NotifyAttackStarted(AttackCDO);
	Loadout->Presentation.CarryState = ERPGWeaponCarryState::Sheathing;
	Loadout->PlayingStowMontage = NewObject<UAnimMontage>();
	Loadout->NotifyAttackStarted(AttackCDO);
	ASC->HandleGameplayEvent(FGameplayTag::RequestGameplayTag(TEXT("Player.Event.Unequip.GreatSword")), &Payload);
	TestEqual(TEXT("Re-attack interrupts sheathing and late notify cannot re-stow"), Weapon->GetAttachParentSocketName(), FName(TEXT("WeaponHandSocket")));
	Loadout->NotifyAttackStarted(GetDefault<URPGGameplayAbility_WeaponStance>());
	TestEqual(TEXT("Non-attack does not change carry"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Drawn);
	ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
	Advance(10.1f);
	TestEqual(TEXT("Death does not start idle animation"), Loadout->Presentation.CarryState, ERPGWeaponCarryState::Drawn);
	Loadout->UnregisterComponent();
	TestFalse(TEXT("Unregister clears owned timer"), World->GetTimerManager().IsTimerActive(Loadout->AutoStowTimer));
	return true;
}

#endif
