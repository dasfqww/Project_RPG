#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "UI/Vitals/RPGPlayerVitalsWidget.h"
#include "UI/Debuffs/RPGPlayerDebuffBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "GameplayEffect/RPGSlowEffect.h"
#include "GameplayEffect/RPGBurnEffect.h"
#include "GameplayEffect/RPGDefenseReductionEffect.h"
#include "GameplayEffect/RPGDamageTakenIncreaseEffect.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

namespace RPGPlayerDebuffHUDProbe
{
struct FProbe
{
	FString Directory;
	double Deadline=FPlatformTime::Seconds()+240.,At=0.;
	int32 Stage=0;
	bool bDone=false;
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	TWeakObjectPtr<URPGPlayerVitalsWidget> Vitals;
	TWeakObjectPtr<URPGPlayerDebuffBarWidget> Bar;
	TArray<TSharedPtr<FJsonValue>> Observations;
	TArray<int32> Counts;
	void Write(const TCHAR* File,const TSharedRef<FJsonObject>& Object)
	{
		FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);
		FFileHelper::SaveStringToFile(Text,*(Directory/File),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	void Fail(const TCHAR* Reason)
	{
		UE_LOG(LogTemp,Error,TEXT("PLAYER_DEBUFF_HUD_FAIL Stage=%d Reason=%s"),Stage,Reason);
		auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),false);R->SetStringField(TEXT("reason"),Reason);Write(TEXT("Preview.result.json"),R);
		bDone=true;FPlatformMisc::RequestExitWithStatus(false,1);
	}
	TSharedRef<FJsonObject> Geometry(UWidget* Widget)
	{
		auto R=MakeShared<FJsonObject>();const auto& G=Widget->GetCachedGeometry();
		const auto P=G.GetAbsolutePosition(),S=G.GetAbsoluteSize();
		R->SetNumberField(TEXT("x"),P.X);R->SetNumberField(TEXT("y"),P.Y);R->SetNumberField(TEXT("width"),S.X);R->SetNumberField(TEXT("height"),S.Y);return R;
	}
	bool Observe(const TCHAR* Name,int32 Expected)
	{
		Bar->RefreshPresentation();const auto& Items=Bar->GetDisplayedDebuffs();if(Items.Num()!=Expected){Fail(TEXT("StatusCount"));return false;}
		auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("name"),Name);R->SetNumberField(TEXT("count"),Items.Num());
		auto* Mana=Vitals->GetWidgetFromName(TEXT("ManaBar"));if(!Mana){Fail(TEXT("ManaBinding"));return false;}
		const auto MP=Geometry(Mana);R->SetObjectField(TEXT("mana"),MP);R->SetObjectField(TEXT("bar"),Geometry(Bar.Get()));
		TArray<TSharedPtr<FJsonValue>> Rows;
		for(const auto& Item:Items)
		{
			const FString Suffix=FString::FromInt(int32(Item.Kind));
			auto* Icon=Bar->GetWidgetFromName(FName(TEXT("DebuffIcon_")+Suffix));
			auto* Time=Bar->GetWidgetFromName(FName(TEXT("DebuffTime_")+Suffix));
			if(!Icon||!Time){Fail(TEXT("IconOrTimeMissing"));return false;}
			auto Row=MakeShared<FJsonObject>();const auto IG=Geometry(Icon),TG=Geometry(Time);
			if(IG->GetNumberField(TEXT("width"))<10||TG->GetNumberField(TEXT("height"))<1
				||TG->GetNumberField(TEXT("y"))<IG->GetNumberField(TEXT("y"))+IG->GetNumberField(TEXT("height"))-.1
				||TG->GetNumberField(TEXT("y"))+TG->GetNumberField(TEXT("height"))>MP->GetNumberField(TEXT("y"))
				||IG->GetNumberField(TEXT("x"))<MP->GetNumberField(TEXT("x"))-2.)
			{Fail(TEXT("IconTimerAboveManaGeometry"));return false;}
			Row->SetNumberField(TEXT("kind"),int32(Item.Kind));Row->SetNumberField(TEXT("remaining"),Item.RemainingSeconds);
			Row->SetObjectField(TEXT("icon"),IG);Row->SetObjectField(TEXT("time"),TG);Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		R->SetArrayField(TEXT("rows"),Rows);Observations.Add(MakeShared<FJsonValueObject>(R));Counts.Add(Expected);
		FScreenshotRequest::RequestScreenshot(Directory/(FString(Name)+TEXT(".png")),true,false);
		UE_LOG(LogTemp,Display,TEXT("PLAYER_DEBUFF_HUD_OBSERVE Name=%s Count=%d"),Name,Expected);return true;
	}
	void Add(TSubclassOf<UGameplayEffect> Type,float Duration)
	{
		auto Spec=ASC->MakeOutgoingSpec(Type,1,ASC->MakeEffectContext());Spec.Data->SetDuration(Duration,true);
		Spec.Data->SetSetByCallerMagnitude(RPGSlow::MagnitudeTag(),.6f);Spec.Data->SetSetByCallerMagnitude(RPGDefenseReduction::MagnitudeTag(),.5f);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
	bool Tick(float)
	{
		if(Directory.IsEmpty())FParse::Value(FCommandLine::Get(),TEXT("RPGDebuffHUDDirectory="),Directory);
		if(bDone){if(IFileManager::Get().FileExists(*(Directory/TEXT("exit")))){FPlatformMisc::RequestExitWithStatus(false,0);return false;}return true;}
		if(FPlatformTime::Seconds()>Deadline){Fail(TEXT("Timeout"));return false;}
		UWorld* World=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game&&C.World())World=C.World();
		if(!World||World->GetNetMode()!=NM_Standalone)return true;
		if(Stage==0)
		{
			if(World->GetMapName()!=TEXT("Entry")||!World->GetAuthGameMode()||World->GetAuthGameMode()->GetClass()!=AGameModeBase::StaticClass())
			{Fail(TEXT("IsolatedEntryRequired"));return false;}
			auto* PC=World->GetFirstPlayerController();APawn* Pawn=PC?PC->GetPawn():nullptr;if(!Pawn||!GEngine->GameViewport)return true;
			if(Pawn->FindComponentByClass<UAbilitySystemComponent>()){Fail(TEXT("EmptyFixturePawnRequired"));return false;}
			auto* A=NewObject<UAbilitySystemComponent>(Pawn);Pawn->AddInstanceComponent(A);A->RegisterComponent();ASC=A;
			auto* Attributes=NewObject<URPGAttributeSet>(Pawn);Attributes->InitMaxHealth(200);Attributes->InitCurrentHealth(200);
			Attributes->InitMaxMana(100);Attributes->InitCurrentMana(100);Attributes->InitDefense(10);Attributes->InitMoveSpeed(1);
			A->AddAttributeSetSubobject(Attributes);A->InitAbilityActorInfo(Pawn,Pawn);
			auto* Type=LoadClass<URPGPlayerVitalsWidget>(nullptr,TEXT("/Game/Blueprints/UI/PlayerHUD/Modules/WBP_PlayerVitals.WBP_PlayerVitals_C"));
			Vitals=CreateWidget<URPGPlayerVitalsWidget>(PC,Type);if(!Vitals.IsValid()||!Vitals->AddToPlayerScreen(10)){Fail(TEXT("VitalsCreate"));return false;}
			Bar=Cast<URPGPlayerDebuffBarWidget>(Vitals->GetWidgetFromName(TEXT("PlayerDebuffs")));
			if(!Bar.IsValid()){Fail(TEXT("NativeDebuffModuleMissing"));return false;}
			At=World->GetTimeSeconds();Stage=1;return true;
		}
		const double Now=World->GetTimeSeconds();
		if(Stage==1&&Now-At>1.)
		{
			Add(URPGSlowEffect::StaticClass(),8);Add(URPGBurnEffect::StaticClass(),5);
			Add(URPGDefenseReductionEffect::StaticClass(),12);Add(URPGDamageTakenIncreaseEffect::StaticClass(),10);
			At=Now;Stage=2;return true;
		}
		if(Stage==2&&Now-At>1.){if(Observe(TEXT("Active"),4))Stage=3;return true;}
		if(Stage==3&&Now-At>5.5){if(Observe(TEXT("ExpiredBurn"),3))Stage=4;return true;}
		if(Stage==4&&Now-At>6.){ASC->RemoveActiveEffects(FGameplayEffectQuery());At=Now;Stage=5;return true;}
		if(Stage==5&&Now-At>1.){if(Observe(TEXT("Cleansed"),0)){At=Now;Stage=6;}return true;}
		if(Stage==6&&Now-At>.5){Add(URPGBurnEffect::StaticClass(),4);At=Now;Stage=7;return true;}
		if(Stage==7&&Now-At>1.){if(Observe(TEXT("Reapplied"),1)){At=Now;Stage=8;}return true;}
		if(Stage==8&&Now-At>.5){ASC->GetAvatarActor()->Destroy();At=Now;Stage=9;return true;}
		if(Stage==9&&Now-At>1.)
		{
			if(!Observe(TEXT("PawnEnded"),0))return true;
			auto R=MakeShared<FJsonObject>();R->SetBoolField(TEXT("passed"),true);R->SetStringField(TEXT("placement"),TEXT("above_right_mana"));
			R->SetArrayField(TEXT("observations"),Observations);R->SetBoolField(TEXT("fixtureAppliedNativeEffects"),true);
			R->SetBoolField(TEXT("networkTest"),false);Write(TEXT("Preview.result.json"),R);bDone=true;
			UE_LOG(LogTemp,Display,TEXT("PLAYER_DEBUFF_HUD_COMPLETE"));
		}
		return true;
	}
};
static const auto Handle=[]()
{
	if(!FParse::Param(FCommandLine::Get(),TEXT("RPGDebuffHUDPreview")))return FTSTicker::FDelegateHandle();
	auto P=MakeShared<FProbe>();return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.03f);
}();
}
#endif
