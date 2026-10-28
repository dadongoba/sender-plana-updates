#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenderCableCarRoute.generated.h"
class USplineComponent;

/** Saved, editable centreline. Runtime movement reads this spline, never the rail meshes. */
UCLASS(meta=(DisplayName="Cable Car Route"))
class SENDER_API ASenderCableCarRoute : public AActor
{
    GENERATED_BODY()
public:
    ASenderCableCarRoute();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cable Car|Route")
    TObjectPtr<USplineComponent> RouteSpline;

    /** Original authoring polygon; rebuild replaces manual spline edits. */
    UPROPERTY(EditAnywhere, Category="Cable Car|Authoring")
    TArray<FVector> SeedPolyline;

    /** Maximum distance trimmed along each side of a corner (cm). */
    UPROPERTY(EditAnywhere, Category="Cable Car|Authoring", meta=(ClampMin="1.0",ClampMax="300.0"))
    float CornerBlendDistance=120.f;

    /** Explicitly regenerate rounded corners from SeedPolyline. Manual spline edits otherwise persist. */
    UFUNCTION(CallInEditor, Category="Cable Car|Authoring")
    void RebuildSmoothRoute();
};
