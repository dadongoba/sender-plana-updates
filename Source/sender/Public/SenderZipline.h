#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenderZipline.generated.h"

class USplineComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;
class UAnimMontage;
class UAnimSequence;
class ACharacter;

/** Where the zipline actor currently is in its throw / ride / release cycle. */
UENUM(BlueprintType)
enum class EZiplineState : uint8
{
    Idle     UMETA(DisplayName = "Idle"),
    Throwing UMETA(DisplayName = "Throwing"),
    Riding   UMETA(DisplayName = "Riding"),
    Reeling  UMETA(DisplayName = "Reeling")
};

/**
 * Zipline with a physical grappling hook.
 *
 * The actor owns three visuals: a hook mesh that normally rests in the player's
 * right hand, a rope drawn as a stretched cylinder, and the spline that defines
 * the wire path. Pressing the interact key throws the hook at the wire, the hook
 * snaps onto it and slides along, dragging the character with it; pressing the
 * key again reels the hook back and drops the character straight down.
 */
UCLASS(Blueprintable, BlueprintType)
class SENDER_API ASenderZipline : public AActor
{
    GENERATED_BODY()

public:
    ASenderZipline();

    virtual void Tick(float DeltaSeconds) override;

    /** Wire path. Point 0 is the start, the last point is the end. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zipline")
    TObjectPtr<USplineComponent> Spline;

    /** The grappling hook mesh. Rests in the hand, flies to the wire, rides it. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zipline|Visual")
    TObjectPtr<USkeletalMeshComponent> HookMesh;

    /** Rope between the hand and the hook, drawn as a stretched cylinder. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zipline|Visual")
    TObjectPtr<UStaticMeshComponent> RopeMesh;

    // ---------------------------------------------------------------- assets

    UPROPERTY(EditDefaultsOnly, Category = "Zipline|Assets")
    TObjectPtr<USkeletalMesh> HookMeshAsset;

    UPROPERTY(EditDefaultsOnly, Category = "Zipline|Assets")
    TObjectPtr<UStaticMesh> RopeMeshAsset;

    UPROPERTY(EditDefaultsOnly, Category = "Zipline|Assets")
    TObjectPtr<UMaterialInterface> RopeMaterial;

    /** Montage played while the hook leaves the hand. */
    UPROPERTY(EditDefaultsOnly, Category = "Zipline|Animation")
    TObjectPtr<UAnimMontage> ThrowMontage;

    /** Looping pose held while riding the wire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Animation")
    TObjectPtr<UAnimSequence> RopeHoldingPose;

    /** Bone / socket on the character mesh that holds the hook. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets")
    FName HandSocket = TEXT("hand_r");

    /** Scale applied to the hook while it is in the hand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets")
    float HandHookScale = 0.55f;

    /** Scale applied to the hook while it rides the wire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets")
    float WireHookScale = 0.55f;

    /** Offset from the hand bone, in the bone's space, where the hook rests. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets")
    FVector HandHookOffset = FVector(-4.f, 0.f, 4.f);

    /** Extra rotation for the hook so its claws line up with the wire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets")
    FRotator HookRotationOffset = FRotator(0.f, 0.f, 0.f);

    /** Radius of the drawn rope, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Assets", meta = (ClampMin = "0.1"))
    float RopeRadius = 0.7f;

    // -------------------------------------------------------------- movement

    /** Slide speed in cm/s while hanging. ALS running speed is 600, sprint ~900. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement", meta = (ClampMin = "50.0"))
    float SlideSpeed = 900.f;

    /** How fast the rider accelerates up to SlideSpeed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement", meta = (ClampMin = "10.0"))
    float SlideAcceleration = 1600.f;

    /** How far below the wire the character's centre hangs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement", meta = (ClampMin = "60.0"))
    float HangOffset = 150.f;

    /**
     * Horizontal offset applied to the rider (character local space: X forward, Y right, Z up),
     * used to slide the body back so the forward-reaching hands of the holding pose land on the wire.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    FVector HangGrabOffset = FVector::ZeroVector;

    /** Bone that should stay glued to the rope while riding. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    FName GrabHandSocket = FName("hand_r");

    /** Snap the grab bone onto the wire every tick, so the hands visually grip the rope. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    bool bAnchorHandToWire = true;

    /** 0 = ignore the bone, 1 = bone sits exactly on the wire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HandAnchorBlend = 1.f;

    /**
     * The holding pose lifts the animated body far above the capsule, which throws the camera off.
     * While riding the mesh is slid by this much so the rendered body sits on the capsule again.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    bool bCompensateMeshOffset = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    float RideMeshOffsetZ = -210.f;

    /** How far above the wire the hook's centre sits, so the claws grip it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Movement")
    float HookRideLift = -10.f;

    // ----------------------------------------------------------- interaction

    /** Max 3D distance between the character and the rope to allow throwing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction", meta = (ClampMin = "50.0"))
    float AttachRadius = 380.f;

    /** Seconds the hook takes to fly from the hand to the wire. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction", meta = (ClampMin = "0.05"))
    float ThrowDuration = 0.5f;

    /** How high the throw arc bulges above the straight line, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    float ThrowArcHeight = 70.f;

    /** Seconds the hook takes to fly back to the hand when releasing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction", meta = (ClampMin = "0.05"))
    float ReelDuration = 0.28f;

    /** Detach automatically when reaching the end instead of waiting for the key. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    bool bAutoDetachAtEnd = false;

    /** Draw the on-screen hint when the character is close enough to throw. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    bool bShowPrompt = true;

    /** Overlay state index on the ALS character that shows the rope-pulling pose. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    int32 RopeOverlayState = 13;

    /** Overlay state index used when the character is empty handed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    int32 DefaultOverlayState = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zipline|Interaction")
    FText PromptText;

    // ----------------------------------------------------------------- state

    UPROPERTY(BlueprintReadOnly, Category = "Zipline")
    EZiplineState State = EZiplineState::Idle;

    UPROPERTY(BlueprintReadOnly, Category = "Zipline")
    bool bRiding = false;

    /** Throw the hook at the wire with the given character, if it is close enough. */
    UFUNCTION(BlueprintCallable, Category = "Zipline")
    bool TryAttach(ACharacter* NewRider);

    /** Release the current rider; the character falls straight down. */
    UFUNCTION(BlueprintCallable, Category = "Zipline")
    void Detach();

    UFUNCTION(BlueprintPure, Category = "Zipline")
    FVector GetStartLocation() const;

    UFUNCTION(BlueprintPure, Category = "Zipline")
    FVector GetEndLocation() const;

    UFUNCTION(BlueprintPure, Category = "Zipline")
    float GetRopeLength() const { return TotalLength; }

    /** Called when the hook bites the wire and the ride starts. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Zipline")
    void OnHookLanded(ACharacter* NewRider);

    /** Called when the hook leaves the wire. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Zipline")
    void OnHookReleased(ACharacter* OldRider);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    void HandleInteract();
    void BindPlayerInput();
    void UpdatePrompt(const FVector& PlayerLoc) const;

    void TickIdle(float DeltaSeconds);
    void TickThrowing(float DeltaSeconds);
    void TickRiding(float DeltaSeconds);
    void TickReeling(float DeltaSeconds);

    void ApplyRopePhysics(ACharacter* Char);
    void BeginRide();
    void SnapRiderToWire();
    void ReleaseRiderMovement();

    /** Rotates HangGrabOffset into world space using the rider's facing (wire yaw). */
    FVector GetGrabWorldOffset() const;

    /** Nudges the rider so the grab bone lands on the wire (uses the pose evaluated this frame). */
    void AnchorGrabBoneToWire(const FVector& WireLocation);

    void UpdateHookAtHand();
    void UpdateHookPoseOnWire();
    void UpdateRopeVisual();
    void SetHookHidden(bool bInHidden);

    /** Writes the ALS overlay state enum on the character through reflection. */
    void SetOverlayState(ACharacter* Char, int32 Value);

    ACharacter* GetPlayerCharacter() const;
    FVector GetHandLocation() const;

private:
    UPROPERTY(Transient)
    TObjectPtr<ACharacter> Rider;

    float TotalLength = 0.f;
    float CurrentDistance = 0.f;
    float CurrentSpeed = 0.f;
    float CachedGravityScale = 1.f;
    bool bCachedUseControllerYaw = true;
    FVector CachedMeshRelativeLocation = FVector::ZeroVector;
    bool bReachedEnd = false;
    bool bInputBound = false;
    int32 PromptKey = 0;
    FTimerHandle BindTimer;

    // Throw / reel animation state.
    float ThrowElapsed = 0.f;
    float ReelElapsed = 0.f;
    FVector ThrowStart = FVector::ZeroVector;
    FVector ThrowControl = FVector::ZeroVector;
    FVector ThrowTarget = FVector::ZeroVector;
    FVector ReelStart = FVector::ZeroVector;
    FVector ReelControl = FVector::ZeroVector;
    FVector ReelTarget = FVector::ZeroVector;
    FVector LastHookLocation = FVector::ZeroVector;
};
