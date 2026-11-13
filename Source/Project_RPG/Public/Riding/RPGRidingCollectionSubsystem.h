#pragma once
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Riding/RPGRidingCollectionReader.h"
#include "RPGRidingCollectionSubsystem.generated.h"

struct FStreamableHandle;

/** Local owner of the lobby read screen. No registration, settings mutation or server authority. */
UCLASS()
class PROJECT_RPG_API URPGRidingCollectionSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    TSharedPtr<FRPGRidingCollectionReader> GetReader();
    void ReleaseReader();
    FText DescribeMount(const FString& Id) const;
private:
    FRPGRidingCollectionContext ReadContext() const;
    bool TickCollection(float);
    void RefreshPresentationAssets();
    TSharedPtr<FRPGRidingCollectionReader> Reader;
    TSharedPtr<FStreamableHandle> PresentationLoad;
    FString PresentationKey;
    FTSTicker::FDelegateHandle TickHandle;
    bool bEnding = false;
};
