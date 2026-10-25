#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenderActor.generated.h"

/** Optional native base for actors implemented with both C++ and Blueprints. */
UCLASS(Blueprintable, BlueprintType)
class SENDER_API ASenderActor : public AActor
{
    GENERATED_BODY()

public:
    /** Override in C++ or Blueprint to implement project-specific interaction. */
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Sender")
    void Interact(AActor* InstigatorActor);
    virtual void Interact_Implementation(AActor* InstigatorActor);
};
