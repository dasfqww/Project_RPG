#include "UI/Lobby/RPGCharacterLineupStage.h"

#include "Animation/AnimSequence.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float SlotSpacing = 178.f;
	const FVector LineupCamera(950, 0, 118);
	const FVector PortraitCamera(410, 0, 130);

	float SlotCenter(int32 Index)
	{
		return (2.5f - Index) * SlotSpacing;
	}
}

ARPGCharacterLineupStage::ARPGCharacterLineupStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f / 24.f;
	SetReplicates(false);
	SetActorEnableCollision(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Warrior(
		TEXT("/Game/Assets/InfinityBladeWarriors/Character/CompleteCharacters/SK_CharM_Ram"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(
		TEXT("/Game/Assets/PlayerAnims/GreatSword/Frank_RPG_GreatSword_Equip_Idle"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sword(TEXT("/Game/Assets/Weapons/LongSword"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Stone(TEXT("/Game/Assets/Arena/Meshes/SM_Floor4m"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pillar(TEXT("/Game/Assets/Arena/Meshes/SM_Pillar05"));

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("LineupCamera"));
	Capture->SetupAttachment(RootComponent);
	Capture->SetRelativeLocation(LineupCamera);
	Capture->SetRelativeRotation(FRotator(0, 180, 0));
	Capture->FOVAngle = 60.f;
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->ShowFlags.SetAtmosphere(false);
	auto& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = 1.f;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_MotionBlurAmount = true;
	Post.MotionBlurAmount = 0.f;
	Post.bOverride_FilmGrainIntensity = true;
	Post.FilmGrainIntensity = 0.f;

	auto AddArchitecture = [this](FName Name, UStaticMesh* Mesh, FVector Anchor,
		FRotator Rotation, FVector Scale, bool bGrounded)
	{
		auto* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(RootComponent);
		Part->SetStaticMesh(Mesh);
		FTransform Transform(Rotation, Anchor, Scale);
		if (Mesh)
		{
			const FBox Bounds = Mesh->GetBoundingBox();
			FVector Pivot = Bounds.GetCenter();
			if (bGrounded) Pivot.Z = Bounds.Min.Z;
			Transform.AddToTranslation(-Transform.TransformVector(Pivot));
		}
		Part->SetRelativeTransform(Transform);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};
	AddArchitecture(TEXT("StoneFloor"), Stone.Object, FVector(145, 0, -5),
		FRotator::ZeroRotator, FVector(3.5, 4.6, 1), false);
	AddArchitecture(TEXT("RearWall"), Stone.Object, FVector(-310, 0, 190),
		FRotator(-90, 0, 0), FVector(2.1, 4.6, 1), false);
	for (int32 Index = 0; Index <= SlotCount; ++Index)
	{
		AddArchitecture(FName(*FString::Printf(TEXT("Pillar%d"), Index)), Pillar.Object,
			FVector(-227, SlotCenter(Index) + SlotSpacing * .5f, -5),
			FRotator::ZeroRotator, FVector(.75, .75, 1.3), true);
	}

	auto AddLight = [this](FName Name, FVector Position, FLinearColor Color, float Intensity, float Radius)
	{
		auto* Light = CreateDefaultSubobject<UPointLightComponent>(Name);
		Light->SetupAttachment(RootComponent);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(Position);
		Light->SetLightColor(Color);
		Light->bUseInverseSquaredFalloff = false;
		Light->SetIntensity(Intensity);
		Light->SetAttenuationRadius(Radius);
		Light->SetCastShadows(false);
		return Light;
	};
	AddLight(TEXT("CoolFill"), FVector(345, 0, 320), FLinearColor(.68f, .78f, 1.f), 5.1f, 1400.f);
	AddLight(TEXT("WallLight"), FVector(-110, 0, 215), FLinearColor(.5f, .68f, 1.f), 3.5f, 1250.f);

	auto AddCharacter = [this](int32 Index, FVector Position, float Yaw)
	{
		auto* Body = CreateDefaultSubobject<USkeletalMeshComponent>(FName(*FString::Printf(TEXT("Warrior%d"), Index)));
		Body->SetupAttachment(RootComponent);
		Body->SetRelativeLocation(Position);
		Body->SetRelativeRotation(FRotator(0, Yaw, 0));
		Body->SetSkeletalMesh(Warrior.Object);
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->OverrideAnimationData(Idle.Object, true, true, .29f * Index);
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		auto* Weapon = CreateDefaultSubobject<UStaticMeshComponent>(FName(*FString::Printf(TEXT("GreatSword%d"), Index)));
		Weapon->SetupAttachment(Body, TEXT("WeaponHandSocket"));
		Weapon->SetStaticMesh(Sword.Object);
		Weapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Body;
	};
	for (int32 Index = 0; Index < SlotCount; ++Index)
	{
		const float Center = SlotCenter(Index);
		Characters.Add(AddCharacter(Index, FVector(0, Center, 0), -90.f - .025f * Center));
		SlotLights.Add(AddLight(FName(*FString::Printf(TEXT("SlotKey%d"), Index)),
			FVector(115, Center, 170), FLinearColor(1.f, .72f, .42f), 3.5f, 265.f));
	}
	CloseupCharacter = AddCharacter(SlotCount, FVector::ZeroVector, -78.f);
}

void ARPGCharacterLineupStage::BeginPlay()
{
	Super::BeginPlay();
	Image = NewObject<UTextureRenderTarget2D>(this);
	Image->ClearColor = FLinearColor(.012f, .018f, .027f, 1.f);
	Image->RenderTargetFormat = RTF_RGBA8_SRGB;
	Image->InitAutoFormat(ImageWidth, ImageHeight);
	Capture->TextureTarget = Image;
	Capture->ShowOnlyActorComponents(this);
}

void ARPGCharacterLineupStage::Present(uint8 OccupiedSlots, int32 SelectedSlot, bool bCloseup, bool bShowCharacter)
{
	CameraTarget = bCloseup ? PortraitCamera : LineupCamera;
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		const bool bVisible = !bCloseup && (OccupiedSlots & (1 << Index)) != 0;
		const bool bSelected = bVisible && Index == SelectedSlot;
		Characters[Index]->SetVisibility(bVisible, true);
		Characters[Index]->SetComponentTickEnabled(bVisible);
		Characters[Index]->SetRelativeLocation(FVector(bSelected ? 23.f : 0.f, SlotCenter(Index), 0));
		SlotLights[Index]->SetVisibility(bSelected);
	}
	CloseupCharacter->SetVisibility(bCloseup && bShowCharacter, true);
	CloseupCharacter->SetComponentTickEnabled(bCloseup && bShowCharacter);
	if (bCloseup) SlotLights[2]->SetVisibility(bShowCharacter);
}

void ARPGCharacterLineupStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Capture->SetRelativeLocation(FMath::VInterpTo(Capture->GetRelativeLocation(), CameraTarget, DeltaSeconds, 8.f));
	if (Image) Capture->CaptureScene();
}
