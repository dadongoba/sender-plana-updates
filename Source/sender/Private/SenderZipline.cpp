#include "SenderZipline.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"

namespace
{
    constexpr float EndTolerance = 1.5f;

    FORCEINLINE FVector QuadBezier(const FVector& A, const FVector& B, const FVector& C, float T)
    {
        const float U = 1.f - T;
        return A * (U * U) + B * (2.f * U * T) + C * (T * T);
    }

    FORCEINLINE float EaseOutCubic(float T)
    {
        const float U = 1.f - FMath::Clamp(T, 0.f, 1.f);
        return 1.f - U * U * U;
    }
}

ASenderZipline::ASenderZipline()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
    SetRootComponent(Spline);
    Spline->SetClosedLoop(false);

    // A straight, 10 m wire by default; edit the spline points in the level instead.
    Spline->ClearSplinePoints(false);
    Spline->AddPoint(FSplinePoint(0.f, FVector::ZeroVector, ESplinePointType::Linear), false);
    Spline->AddPoint(FSplinePoint(1.f, FVector(0.f, 0.f, 1000.f), ESplinePointType::Linear), false);
    Spline->UpdateSpline();

    HookMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("HookMesh"));
    HookMesh->SetupAttachment(Spline);
    HookMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HookMesh->SetGenerateOverlapEvents(false);
    HookMesh->SetCastShadow(false);
    HookMesh->SetVisibility(false);

    RopeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RopeMesh"));
    RopeMesh->SetupAttachment(Spline);
    RopeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RopeMesh->SetGenerateOverlapEvents(false);
    RopeMesh->SetCastShadow(false);
    RopeMesh->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> HookFinder(
        TEXT("/Game/AdvancedLocomotionV4/Props/Meshes/Hook/Hook"));
    if (HookFinder.Succeeded())
    {
        HookMeshAsset = HookFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> RopeFinder(
        TEXT("/Engine/BasicShapes/Cylinder"));
    if (RopeFinder.Succeeded())
    {
        RopeMeshAsset = RopeFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> RopeMatFinder(
        TEXT("/Game/AdvancedLocomotionV4/Props/Materials/Other/M_Prop_Hook_Material"));
    if (RopeMatFinder.Succeeded())
    {
        RopeMaterial = RopeMatFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimMontage> ThrowFinder(
        TEXT("/Game/AdvancedLocomotionV4/CharacterAssets/Animations/Climbing_System/RopeSwinging/ALS_RopeSwing_ThrowHook_01_Montage"));
    if (ThrowFinder.Succeeded())
    {
        ThrowMontage = ThrowFinder.Object;
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> PoseFinder(
        TEXT("/Game/Zipline/Start_Swinging__1__Anim_A2"));
    if (PoseFinder.Succeeded())
    {
        RopeHoldingPose = PoseFinder.Object;
    }

    PromptText = FText::FromString(TEXT("Press [F] to throw the hook"));
}

void ASenderZipline::BeginPlay()
{
    Super::BeginPlay();

    PromptKey = static_cast<int32>(GetUniqueID() & 0x7fffffff);

    if (HookMesh)
    {
        if (HookMeshAsset)
        {
            HookMesh->SetSkinnedAssetAndUpdate(HookMeshAsset);
        }
        HookMesh->SetVisibility(false);
    }

    if (RopeMesh)
    {
        if (RopeMeshAsset)
        {
            RopeMesh->SetStaticMesh(RopeMeshAsset);
        }
        if (RopeMaterial)
        {
            RopeMesh->SetMaterial(0, RopeMaterial);
        }
        RopeMesh->SetVisibility(false);
    }

    Spline->UpdateSpline();
    TotalLength = Spline->GetSplineLength();

    BindPlayerInput();
    if (!bInputBound)
    {
        GetWorldTimerManager().SetTimer(BindTimer, this, &ASenderZipline::BindPlayerInput, 0.25f, true);
    }
}

void ASenderZipline::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(BindTimer);

    if (State == EZiplineState::Riding)
    {
        ReleaseRiderMovement();
    }
    Rider = nullptr;
    State = EZiplineState::Idle;
    bRiding = false;

    Super::EndPlay(EndPlayReason);
}

void ASenderZipline::BindPlayerInput()
{
    if (bInputBound)
    {
        return;
    }
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }

    EnableInput(PC);
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ASenderZipline::HandleInteract);
        bInputBound = true;
        GetWorldTimerManager().ClearTimer(BindTimer);
    }
}

void ASenderZipline::HandleInteract()
{
    if (State == EZiplineState::Riding && Rider)
    {
        Detach();
        return;
    }
    if (State == EZiplineState::Idle)
    {
        if (ACharacter* PlayerCharacter = GetPlayerCharacter())
        {
            TryAttach(PlayerCharacter);
        }
    }
}

ACharacter* ASenderZipline::GetPlayerCharacter() const
{
    return Cast<ACharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
}

FVector ASenderZipline::GetHandLocation() const
{
    ACharacter* Char = Rider ? Rider.Get() : GetPlayerCharacter();
    if (!Char)
    {
        return GetActorLocation();
    }
    if (USkeletalMeshComponent* SkelMesh = Char->GetMesh())
    {
        if (!HandSocket.IsNone() && SkelMesh->DoesSocketExist(HandSocket))
        {
            return SkelMesh->GetSocketLocation(HandSocket);
        }
        // Fall back to the component location if the bone is missing.
        return SkelMesh->GetComponentLocation();
    }
    return Char->GetActorLocation();
}

void ASenderZipline::SetOverlayState(ACharacter* Char, int32 Value)
{
    if (!Char)
    {
        return;
    }
    UClass* Cls = Char->GetClass();
    if (!Cls)
    {
        return;
    }

    FProperty* Prop = Cls->FindPropertyByName(FName(TEXT("OverlayState")));
    if (!Prop)
    {
        Prop = Cls->FindPropertyByName(FName(TEXT("overlay_state")));
    }
    if (!Prop)
    {
        return;
    }

    void* Addr = Prop->ContainerPtrToValuePtr<void>(Char);
    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
    {
        if (FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty())
        {
            Underlying->SetIntPropertyValue(Addr, static_cast<int64>(Value));
        }
    }
    else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
    {
        ByteProp->SetPropertyValue(Addr, static_cast<uint8>(Value));
    }
    else if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
    {
        NumProp->SetIntPropertyValue(Addr, static_cast<int64>(Value));
    }
}

bool ASenderZipline::TryAttach(ACharacter* NewRider)
{
    if (!NewRider || State != EZiplineState::Idle || TotalLength <= KINDA_SMALL_NUMBER)
    {
        return false;
    }
    UCharacterMovementComponent* MoveComp = NewRider->GetCharacterMovement();
    if (!MoveComp)
    {
        return false;
    }

    const FVector CharacterLocation = NewRider->GetActorLocation();
    const float Key = Spline->FindInputKeyClosestToWorldLocation(CharacterLocation);
    const FVector ClosestPoint = Spline->GetLocationAtSplineInputKey(Key, ESplineCoordinateSpace::World);
    if (FVector::Dist(CharacterLocation, ClosestPoint) > AttachRadius)
    {
        return false;
    }

    Rider = NewRider;
    bRiding = false;
    bReachedEnd = false;
    State = EZiplineState::Throwing;
    ThrowElapsed = 0.f;
    CurrentSpeed = 0.f;

    CurrentDistance = FMath::Clamp(
        Spline->GetDistanceAlongSplineAtSplineInputKey(Key), 0.f, TotalLength);

    // Throw from the hand to the point on the wire the player is standing under.
    ThrowStart = GetHandLocation();
    ThrowTarget = ClosestPoint + FVector(0.f, 0.f, HookRideLift);
    ThrowControl = (ThrowStart + ThrowTarget) * 0.5f + FVector(0.f, 0.f, ThrowArcHeight);
    LastHookLocation = ThrowStart;

    // Freeze gravity so the character stays put while the hook is in the air.
    CachedGravityScale = MoveComp->GravityScale;
    bCachedUseControllerYaw = NewRider->bUseControllerRotationYaw;

    NewRider->bUseControllerRotationYaw = false;
    MoveComp->GravityScale = 0.f;
    MoveComp->StopMovementImmediately();
    MoveComp->SetMovementMode(MOVE_Falling);

    SetOverlayState(NewRider, RopeOverlayState);

    // Throw animation: the character swings the hook out of the hand.
    if (USkeletalMeshComponent* CharacterMesh = NewRider->GetMesh())
    {
        if (ThrowMontage)
        {
            if (UAnimInstance* AnimInst = CharacterMesh->GetAnimInstance())
            {
                AnimInst->Montage_Play(ThrowMontage, 1.f);
            }
        }
    }

    if (HookMesh)
    {
        HookMesh->SetWorldScale3D(FVector(WireHookScale));
        HookMesh->SetVisibility(true);
    }
    return true;
}

void ASenderZipline::BeginRide()
{
    if (!Rider)
    {
        State = EZiplineState::Idle;
        return;
    }

    State = EZiplineState::Riding;
    bRiding = true;
    bReachedEnd = false;
    CurrentSpeed = 0.f;

    // Hold the rope-pulling pose for the whole ride.
    if (USkeletalMeshComponent* CharacterMesh = Rider->GetMesh())
    {
        CachedMeshRelativeLocation = CharacterMesh->GetRelativeLocation();
        if (bCompensateMeshOffset)
        {
            CharacterMesh->SetRelativeLocation(
                CachedMeshRelativeLocation + FVector(0.f, 0.f, RideMeshOffsetZ), false, nullptr, ETeleportType::TeleportPhysics);
        }
        if (RopeHoldingPose)
        {
            CharacterMesh->PlayAnimation(RopeHoldingPose, true);
        }
    }

    SnapRiderToWire();
    OnHookLanded(Rider);
}

FVector ASenderZipline::GetGrabWorldOffset() const
{
    if (!Spline || HangGrabOffset.IsNearlyZero())
    {
        return FVector::ZeroVector;
    }
    const FVector WireDirection = Spline->GetDirectionAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
    const FRotator YawOnly(0.f, WireDirection.Rotation().Yaw, 0.f);
    return YawOnly.RotateVector(HangGrabOffset);
}

void ASenderZipline::AnchorGrabBoneToWire(const FVector& WireLocation)
{
    if (!bAnchorHandToWire || HandAnchorBlend <= 0.f || !Rider)
    {
        return;
    }

    const USkeletalMeshComponent* CharacterMesh = Rider->GetMesh();
    if (!CharacterMesh)
    {
        return;
    }

    // GetSocketLocation resolves plain bones too, so "hand_r" works without a socket asset.
    const FVector GrabLocation = CharacterMesh->GetSocketLocation(GrabHandSocket);
    if (GrabLocation.IsNearlyZero())
    {
        return;
    }

    // Work with the bone's offset inside the actor so the answer does not depend on where the
    // actor happens to be right now (the skeletal pose is still one frame old).
    const FVector ActorLocation = Rider->GetActorLocation();
    const FRotator ActorRotation = Rider->GetActorRotation();
    const FVector GrabLocal =
        FRotator(0.f, ActorRotation.Yaw, 0.f).UnrotateVector(GrabLocation - ActorLocation);

    const FVector DesiredLocation = WireLocation - FRotator(0.f, ActorRotation.Yaw, 0.f).RotateVector(GrabLocal);
    const FVector NewLocation = FMath::Lerp(ActorLocation, DesiredLocation, FMath::Clamp(HandAnchorBlend, 0.f, 1.f));
    Rider->SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
}

void ASenderZipline::SnapRiderToWire()
{
    if (!Rider)
    {
        return;
    }
    const FVector WireLocation = Spline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
    Rider->SetActorLocation(WireLocation + GetGrabWorldOffset() - FVector(0.f, 0.f, HangOffset), false, nullptr, ETeleportType::TeleportPhysics);
    AnchorGrabBoneToWire(WireLocation);
}

void ASenderZipline::ReleaseRiderMovement()
{
    if (!Rider)
    {
        return;
    }

    if (UCharacterMovementComponent* MoveComp = Rider->GetCharacterMovement())
    {
        MoveComp->GravityScale = CachedGravityScale;
        // No horizontal momentum: drop straight down, as requested.
        MoveComp->Velocity = FVector::ZeroVector;
        MoveComp->SetMovementMode(MOVE_Falling);
    }
    Rider->bUseControllerRotationYaw = bCachedUseControllerYaw;

    // Hand the body back to the locomotion state machine.
    if (USkeletalMeshComponent* CharacterMesh = Rider->GetMesh())
    {
        if (bCompensateMeshOffset)
        {
            CharacterMesh->SetRelativeLocation(CachedMeshRelativeLocation, false, nullptr, ETeleportType::TeleportPhysics);
        }
        if (CharacterMesh->GetAnimationMode() == EAnimationMode::AnimationSingleNode)
        {
            CharacterMesh->SetAnimation(nullptr);
            CharacterMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        }
    }

    SetOverlayState(Rider, DefaultOverlayState);
}

void ASenderZipline::Detach()
{
    if (!Rider || State != EZiplineState::Riding)
    {
        return;
    }

    ACharacter* OldRider = Rider;
    ReleaseRiderMovement();

    bRiding = false;
    bReachedEnd = false;
    CurrentSpeed = 0.f;

    // Start reeling the hook back to the hand.
    ReelStart = LastHookLocation;
    ReelTarget = GetHandLocation();
    ReelControl = (ReelStart + ReelTarget) * 0.5f + FVector(0.f, 0.f, ThrowArcHeight * 0.35f);
    ReelElapsed = 0.f;
    State = EZiplineState::Reeling;

    OnHookReleased(OldRider);
}

void ASenderZipline::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    switch (State)
    {
    case EZiplineState::Idle:
        TickIdle(DeltaSeconds);
        break;
    case EZiplineState::Throwing:
        TickThrowing(DeltaSeconds);
        break;
    case EZiplineState::Riding:
        TickRiding(DeltaSeconds);
        break;
    case EZiplineState::Reeling:
        TickReeling(DeltaSeconds);
        break;
    }
}

void ASenderZipline::TickIdle(float DeltaSeconds)
{
    UpdateHookAtHand();
    LastHookLocation = HookMesh ? HookMesh->GetComponentLocation() : GetActorLocation();

    if (RopeMesh && RopeMesh->IsVisible())
    {
        RopeMesh->SetVisibility(false);
    }

    if (bShowPrompt && GEngine && TotalLength > KINDA_SMALL_NUMBER)
    {
        if (const ACharacter* PlayerCharacter = GetPlayerCharacter())
        {
            UpdatePrompt(PlayerCharacter->GetActorLocation());
        }
    }
}

void ASenderZipline::TickThrowing(float DeltaSeconds)
{
    ThrowElapsed += DeltaSeconds;

    const float T = FMath::Clamp(ThrowElapsed / FMath::Max(ThrowDuration, 0.01f), 0.f, 1.f);
    const float E = EaseOutCubic(T);
    const FVector HookLocation = QuadBezier(ThrowStart, ThrowControl, ThrowTarget, E);

    if (HookMesh)
    {
        const FVector FlightDir = (ThrowTarget - ThrowStart).GetSafeNormal();
        const FRotator HookRot = FlightDir.IsNearlyZero() ? FRotator::ZeroRotator : FlightDir.Rotation();
        HookMesh->SetWorldLocationAndRotation(HookLocation, HookRot + HookRotationOffset);
        HookMesh->SetWorldScale3D(FVector(WireHookScale));
        HookMesh->SetVisibility(true);
    }

    LastHookLocation = HookLocation;
    UpdateRopeVisual();

    if (T >= 1.f)
    {
        BeginRide();
    }
}

void ASenderZipline::TickRiding(float DeltaSeconds)
{
    if (!Rider)
    {
        State = EZiplineState::Idle;
        return;
    }

    CurrentSpeed = FMath::FInterpConstantTo(CurrentSpeed, SlideSpeed, DeltaSeconds, SlideAcceleration);
    CurrentDistance = FMath::Min(CurrentDistance + CurrentSpeed * DeltaSeconds, TotalLength);

    const FVector WireLocation = Spline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
    const FVector WireDirection = Spline->GetDirectionAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);

    // Hook rides on top of the wire, claws down over it.
    const FVector HookLocation = WireLocation + FVector(0.f, 0.f, HookRideLift);
    if (HookMesh)
    {
        HookMesh->SetWorldLocationAndRotation(HookLocation, WireDirection.Rotation() + HookRotationOffset);
        HookMesh->SetWorldScale3D(FVector(WireHookScale));
    }
    LastHookLocation = HookLocation;

    // Character hangs below the wire, pulled along by the hook.
    Rider->SetActorLocation(WireLocation + GetGrabWorldOffset() - FVector(0.f, 0.f, HangOffset), false, nullptr, ETeleportType::TeleportPhysics);
    if (UCharacterMovementComponent* MoveComp = Rider->GetCharacterMovement())
    {
        MoveComp->Velocity = FVector::ZeroVector;
        MoveComp->SetMovementMode(MOVE_Falling);
    }

    if (WireDirection.SizeSquared2D() > KINDA_SMALL_NUMBER)
    {
        Rider->SetActorRotation(FRotator(0.f, WireDirection.Rotation().Yaw, 0.f));
    }

    AnchorGrabBoneToWire(WireLocation);

    UpdateRopeVisual();

    if (CurrentDistance >= TotalLength - EndTolerance && !bReachedEnd)
    {
        bReachedEnd = true;
        if (bAutoDetachAtEnd)
        {
            Detach();
            return;
        }
    }

    if (bShowPrompt && GEngine)
    {
        GEngine->AddOnScreenDebugMessage(
            PromptKey + 1, 0.2f, FColor(255, 210, 120),
            bReachedEnd ? TEXT("Press [F] to drop down") : TEXT("Press [F] to let go"));
    }
}

void ASenderZipline::TickReeling(float DeltaSeconds)
{
    ReelElapsed += DeltaSeconds;

    // The hand keeps moving (the character is falling), so track it every frame.
    ReelTarget = GetHandLocation();
    ReelControl = (ReelStart + ReelTarget) * 0.5f + FVector(0.f, 0.f, ThrowArcHeight * 0.35f);

    const float T = FMath::Clamp(ReelElapsed / FMath::Max(ReelDuration, 0.01f), 0.f, 1.f);
    const float E = EaseOutCubic(T);
    const FVector HookLocation = QuadBezier(ReelStart, ReelControl, ReelTarget, E);

    if (HookMesh)
    {
        const FVector FlightDir = (ReelTarget - ReelStart).GetSafeNormal();
        const FRotator HookRot = FlightDir.IsNearlyZero() ? FRotator::ZeroRotator : FlightDir.Rotation();
        HookMesh->SetWorldLocationAndRotation(HookLocation, HookRot + HookRotationOffset);
        HookMesh->SetWorldScale3D(FVector(FMath::Lerp(WireHookScale, HandHookScale, E)));
        HookMesh->SetVisibility(T < 1.f);
    }

    LastHookLocation = HookLocation;
    UpdateRopeVisual();

    if (T >= 1.f)
    {
        Rider = nullptr;
        State = EZiplineState::Idle;
        if (RopeMesh)
        {
            RopeMesh->SetVisibility(false);
        }
        UpdateHookAtHand();
    }
}

void ASenderZipline::UpdateHookAtHand()
{
    if (!HookMesh)
    {
        return;
    }

    ACharacter* Char = GetPlayerCharacter();
    USkeletalMeshComponent* SkelMesh = Char ? Char->GetMesh() : nullptr;
    if (!SkelMesh)
    {
        HookMesh->SetVisibility(false);
        return;
    }

    FVector HandLocation = Char->GetActorLocation();
    FRotator HandRotation = Char->GetActorRotation();

    if (!HandSocket.IsNone() && SkelMesh->DoesSocketExist(HandSocket))
    {
        HandLocation = SkelMesh->GetSocketLocation(HandSocket);
        HandRotation = SkelMesh->GetSocketRotation(HandSocket);
    }
    else
    {
        HandLocation = Char->GetActorLocation() + FVector(0.f, 30.f, 20.f);
    }

    HookMesh->SetWorldLocationAndRotation(
        HandLocation + HandRotation.RotateVector(HandHookOffset),
        HandRotation + HookRotationOffset);
    HookMesh->SetWorldScale3D(FVector(HandHookScale));
    HookMesh->SetVisibility(true);
}

void ASenderZipline::UpdateHookPoseOnWire()
{
    if (!HookMesh)
    {
        return;
    }
    const FVector WireLocation = Spline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
    const FVector WireDirection = Spline->GetDirectionAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
    HookMesh->SetWorldLocationAndRotation(
        WireLocation + FVector(0.f, 0.f, HookRideLift),
        WireDirection.Rotation() + HookRotationOffset);
    HookMesh->SetWorldScale3D(FVector(WireHookScale));
}

void ASenderZipline::UpdateRopeVisual()
{
    if (!RopeMesh || !RopeMesh->GetStaticMesh())
    {
        return;
    }

    const FVector RopeStart = GetHandLocation();
    const FVector RopeEnd = LastHookLocation;
    const FVector Delta = RopeEnd - RopeStart;
    const float Length = Delta.Size();

    if (Length < 2.f)
    {
        RopeMesh->SetVisibility(false);
        return;
    }

    RopeMesh->SetVisibility(true);
    RopeMesh->SetWorldLocationAndRotation(
        (RopeStart + RopeEnd) * 0.5f,
        FRotationMatrix::MakeFromZ(Delta).Rotator());

    // The engine cylinder is 100 cm tall with a 50 cm radius.
    const float RadiusScale = RopeRadius / 50.f;
    RopeMesh->SetWorldScale3D(FVector(RadiusScale, RadiusScale, Length / 100.f));
}

void ASenderZipline::SetHookHidden(bool bInHidden)
{
    if (HookMesh)
    {
        HookMesh->SetVisibility(!bInHidden);
    }
}

void ASenderZipline::UpdatePrompt(const FVector& PlayerLoc) const
{
    if (!GEngine || TotalLength <= KINDA_SMALL_NUMBER)
    {
        return;
    }
    const FVector ClosestPoint = Spline->FindLocationClosestToWorldLocation(PlayerLoc, ESplineCoordinateSpace::World);
    if (FVector::Dist(ClosestPoint, PlayerLoc) <= AttachRadius)
    {
        GEngine->AddOnScreenDebugMessage(PromptKey, 0.2f, FColor::Cyan, PromptText.ToString());
    }
}

FVector ASenderZipline::GetStartLocation() const
{
    return Spline ? Spline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World) : GetActorLocation();
}

FVector ASenderZipline::GetEndLocation() const
{
    if (!Spline)
    {
        return GetActorLocation();
    }
    const int32 LastIndex = Spline->GetNumberOfSplinePoints() - 1;
    return Spline->GetLocationAtSplinePoint(LastIndex, ESplineCoordinateSpace::World);
}
