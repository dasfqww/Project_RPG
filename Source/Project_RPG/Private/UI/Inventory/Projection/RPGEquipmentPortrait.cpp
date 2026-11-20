#include "UI/Inventory/Projection/RPGEquipmentPortrait.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Character.h"
#include "PreviewScene.h"

namespace
{
	UMeshComponent* MakeMeshCopy(UMeshComponent& Source)
	{
		if (const auto* Skeletal = Cast<USkeletalMeshComponent>(&Source))
		{
			if (!Skeletal->GetSkeletalMeshAsset()) return nullptr;
			auto* Copy = NewObject<UPoseableMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Copy->SetSkinnedAssetAndUpdate(Skeletal->GetSkeletalMeshAsset());
			return Copy;
		}
		if (const auto* Static = Cast<UStaticMeshComponent>(&Source))
		{
			if (!Static->GetStaticMesh()) return nullptr;
			auto* Copy = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			Copy->SetStaticMesh(Static->GetStaticMesh());
			return Copy;
		}
		return nullptr;
	}

	bool HasRenderableMesh(const UMeshComponent& Mesh)
	{
		if (const auto* Skeletal = Cast<USkeletalMeshComponent>(&Mesh)) return Skeletal->GetSkeletalMeshAsset() != nullptr;
		if (const auto* Static = Cast<UStaticMeshComponent>(&Mesh)) return Static->GetStaticMesh() != nullptr;
		return false;
	}
}

FRPGEquipmentPortrait::FRPGEquipmentPortrait()
{
	Scene = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues()
		.SetEditor(false).SetTransactional(false).SetCreatePhysicsScene(false)
		.SetForceMipsResident(false).SetLightRotation(FRotator(-30.f, 155.f, 0.f))
		.SetLightBrightness(4.f));
	Scene->DirectionalLight->SetCastShadows(false);
	auto* Fill = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Fill->SetMobility(EComponentMobility::Movable);
	Fill->SetIntensity(2.f);
	Fill->SetLightColor(FLinearColor(.65f, .76f, 1.f));
	Fill->SetCastShadows(false);
	Scene->AddComponent(Fill, FTransform(FRotator(-15.f, -120.f, 0.f)));

	Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
	Target->RenderTargetFormat = RTF_RGBA8_SRGB;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(288, 480);

	Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage(), NAME_None, RF_Transient);
	Capture->TextureTarget = Target.Get();
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	Capture->ShowFlags.SetTemporalAA(false);
	FPostProcessSettings& Post = Capture->PostProcessSettings;
	Post.bOverride_AutoExposureMethod = true;
	Post.AutoExposureMethod = AEM_Manual;
	Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Post.AutoExposureApplyPhysicalCameraExposure = false;
	Post.bOverride_AutoExposureBias = true;
	Post.AutoExposureBias = .35f;
	// Balance the portrait's warm highlights without replacing the character's materials.
	Post.bOverride_ColorGain = true;
	Post.ColorGain = FVector4(.92f, .97f, 1.08f, 1.f);
	Post.bOverride_FilmGrainIntensity = true;
	Post.FilmGrainIntensity = 0.f;
	// Metallic armor needs an environment to reflect in the isolated scene.
	Post.AmbientCubemap = LoadObject<UTextureCube>(nullptr, TEXT("/Engine/EngineMaterials/DefaultCubemap.DefaultCubemap"));
	Post.bOverride_AmbientCubemapIntensity = true;
	Post.AmbientCubemapIntensity = 1.f;
	Scene->AddComponent(Capture, FTransform::Identity);
}

FRPGEquipmentPortrait::~FRPGEquipmentPortrait() = default;

UTextureRenderTarget2D* FRPGEquipmentPortrait::Draw(ACharacter& Character, float AspectRatio)
{
	auto* Body = Character.GetMesh();
	if (!IsValid(Body) || !Body->IsRegistered() || !Body->GetSkeletalMeshAsset()) return nullptr;

	TArray<USceneComponent*> Attachments;
	Body->GetChildrenComponents(true, Attachments);
	TArray<UMeshComponent*> VisibleMeshes{Body};
	for (USceneComponent* Attachment : Attachments)
	{
		auto* Mesh = Cast<UMeshComponent>(Attachment);
		const AActor* Owner = IsValid(Mesh) ? Mesh->GetOwner() : nullptr;
		if (IsValid(Owner) && Mesh->IsRegistered() && Mesh->IsVisible()
			&& (Owner == &Character || (Owner->GetOwner() == &Character && !Owner->IsHidden()))
			&& HasRenderableMesh(*Mesh))
		{
			VisibleMeshes.Add(Mesh);
		}
	}

	for (auto It = MeshCopies.CreateIterator(); It; ++It)
	{
		if (!VisibleMeshes.Contains(It.Key().Get()))
		{
			Scene->RemoveComponent(It.Value());
			It.RemoveCurrent();
		}
	}

	// Use actor yaw for facing, while keeping the visual scale and socket placement.
	const FTransform ActorFrame(FRotator(0.f, Character.GetActorRotation().Yaw, 0.f), Character.GetActorLocation());
	for (UMeshComponent* Source : VisibleMeshes)
	{
		const FTransform Transform = Source->GetComponentTransform().GetRelativeTransform(ActorFrame);
		UMeshComponent*& Copy = MeshCopies.FindOrAdd(Source);
		if (!Copy)
		{
			Copy = MakeMeshCopy(*Source);
			Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Copy->SetGenerateOverlapEvents(false);
			Copy->SetCastShadow(false);
			Scene->AddComponent(Copy, Transform);
		}

		if (auto* SkeletalCopy = Cast<UPoseableMeshComponent>(Copy))
		{
			auto* Skeletal = CastChecked<USkeletalMeshComponent>(Source);
			if (SkeletalCopy->GetSkinnedAsset() != Skeletal->GetSkeletalMeshAsset())
			{
				SkeletalCopy->SetSkinnedAssetAndUpdate(Skeletal->GetSkeletalMeshAsset());
			}
			SkeletalCopy->CopyPoseFromSkeletalComponent(Skeletal);
			SkeletalCopy->RefreshBoneTransforms();
		}
		else
		{
			auto* StaticCopy = CastChecked<UStaticMeshComponent>(Copy);
			UStaticMesh* Asset = CastChecked<UStaticMeshComponent>(Source)->GetStaticMesh();
			if (StaticCopy->GetStaticMesh() != Asset) StaticCopy->SetStaticMesh(Asset);
		}
		Copy->SetWorldTransform(Transform);
		for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
		{
			if (Copy->GetMaterial(Index) != Source->GetMaterial(Index))
				Copy->SetMaterial(Index, Source->GetMaterial(Index));
		}
	}

	const int32 Height = 480;
	const int32 Width = FMath::Clamp(FMath::RoundToInt(Height * AspectRatio), 64, 480);
	if (Target->SizeX != Width) Target->ResizeTarget(Width, Height);

	const FBoxSphereBounds BodyBounds = Body->GetSkeletalMeshAsset()->GetBounds().TransformBy(
		Body->GetComponentTransform().GetRelativeTransform(ActorFrame));
	const FVector CameraOffset = FVector(1.f, .31f, .04f).GetSafeNormal();
	Capture->OrthoWidth = 2.24f * FMath::Max(BodyBounds.BoxExtent.Z * Width / Height, BodyBounds.BoxExtent.Y);
	Capture->SetWorldLocationAndRotation(BodyBounds.Origin + 340.f * CameraOffset, (-CameraOffset).Rotation());
	Capture->CaptureScene();
	return Target.Get();
}
