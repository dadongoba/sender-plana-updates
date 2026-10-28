#include "SenderCableCarSubsystem.h"
#include "SenderCableCarRoute.h"
#include "Components/SplineComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Stats/Stats.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderCableCar, Log, All);

namespace
{
    int32 AttachDepth(const AActor* A)
    {
        int32 N=0;
        while (A && A->GetAttachParentActor() && N<100) { ++N; A=A->GetAttachParentActor(); }
        return N;
    }
}

bool USenderCableCarSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* W=Cast<UWorld>(Outer);
    return W && W->IsGameWorld();
}
void USenderCableCarSubsystem::OnWorldBeginPlay(UWorld& W)
{
    Super::OnWorldBeginPlay(W);
    State=ECableCarState::Waiting;
    bBuilt=false; bOffsetsCaptured=false; bHeadingHome=false;
    Travelled=CurrentSpeed=WaitRemaining=0.f; CompletedLoops=0;
    FTimerHandle H;
    W.GetTimerManager().SetTimer(H,this,&USenderCableCarSubsystem::CollectActors,0.3f,false);
}
void USenderCableCarSubsystem::Deinitialize()
{
    TrackActors.Reset(); CarActors.Reset(); PathPoints.Reset();
    CarStartTransforms.Reset(); MovementSpline=nullptr;
    Super::Deinitialize();
}
bool USenderCableCarSubsystem::ActorMatches(const AActor* A,FName Tag,const FString& Folder,const FString& Label) const
{
    if (!Tag.IsNone() && A->ActorHasTag(Tag)) return true;
#if WITH_EDITOR
    const FString F=A->GetFolderPath().ToString();
    if (!Folder.IsEmpty() && (F.Equals(Folder,ESearchCase::IgnoreCase) || F.StartsWith(Folder+TEXT("/"),ESearchCase::IgnoreCase))) return true;
#endif
    return !Label.IsEmpty() && A->GetActorNameOrLabel().Equals(Label,ESearchCase::IgnoreCase);
}
void USenderCableCarSubsystem::CollectActors()
{
    TrackActors.Reset(); CarActors.Reset(); StartPoint=nullptr; StopPoint=nullptr;
    AActor* Roof=nullptr;
    for (TActorIterator<AActor> It(GetWorld());It;++It)
    {
        AActor* A=*It;
        if (!IsValid(A)) continue;
        if (ActorMatches(A,StopTag,TEXT(""),StopLabel)) { StopPoint=A; continue; }
        if (ActorMatches(A,StartTag,TEXT(""),StartLabel)) { StartPoint=A; continue; }
        if (ActorMatches(A,TrackTag,TrackFolder,TEXT(""))) TrackActors.Add(A);
        if (ActorMatches(A,CarTag,CarFolder,CarRootLabel)) CarActors.AddUnique(A);
        if (A->GetActorNameOrLabel().Equals(CarRootLabel,ESearchCase::IgnoreCase)) Roof=A;
    }
    // Include the whole keyb hierarchy even when its children use other folders.
    if (Roof)
    {
        CarActors.AddUnique(Roof);
        TArray<AActor*> Children; Roof->GetAttachedActors(Children,true,true);
        for (AActor* Child:Children) if(Child!=StopPoint && Child!=StartPoint) CarActors.AddUnique(Child);
    }
    CarActors.Remove(StopPoint); CarActors.Remove(StartPoint);
    CarActors.Sort([](const AActor& A,const AActor& B) { return AttachDepth(&A)<AttachDepth(&B); });
    UE_LOG(LogSenderCableCar,Log,TEXT("Loop v2 scan: tracks=%d car=%d roof=%s stop=%s"),TrackActors.Num(),CarActors.Num(),Roof?*Roof->GetName():TEXT("missing"),StopPoint?*StopPoint->GetName():TEXT("missing"));
    if (!Roof || !StopPoint || CarActors.IsEmpty()) return;
    const FBox RoofBounds=Roof->GetComponentsBoundingBox(true);
    CarOrigin=RoofBounds.GetCenter(); LastCarOrigin=InitialOrigin=CarOrigin;
    for(AActor* A:CarActors)
    {
        TArray<UPrimitiveComponent*> Cs; A->GetComponents(Cs);
        for(UPrimitiveComponent* C:Cs) C->SetMobility(EComponentMobility::Movable);
    }
    ApplyInitialOffsets(); BuildPath();
    bBuilt=MovementSpline && TotalLength>100.f && StopDistance>1.f && StopDistance<TotalLength-1.f;
    if(!bBuilt) UE_LOG(LogSenderCableCar,Error,TEXT("Closed track could not be resolved; car will stay parked (no straight-line fallback)."));
}
void USenderCableCarSubsystem::ApplyInitialOffsets()
{
    CarStartLocations.Reset(); CarStartTransforms.Reset();
    FBox Bounds(ForceInit);
    for(const AActor* A:CarActors)
    {
        CarStartLocations.Add(A->GetActorLocation()); CarStartTransforms.Add(A->GetActorTransform());
        Bounds+=A->GetComponentsBoundingBox(true);
    }
    // Roof centre is above the usable volume. Keep the trigger away from walls/platform edges.
    CarInterior=FBox(Bounds.Min-InitialOrigin,Bounds.Max-InitialOrigin);
    CarInterior.Min.X+=20.; CarInterior.Min.Y+=20.; CarInterior.Min.Z+=25.;
    CarInterior.Max.X-=20.; CarInterior.Max.Y-=20.; CarInterior.Max.Z-=35.;
    CurrentRotation=FQuat::Identity; bOffsetsCaptured=true;
}
void USenderCableCarSubsystem::BuildPath()
{
    MovementSpline=nullptr; TotalLength=0.f; StopDistance=0.f;
    ASenderCableCarRoute* Route=nullptr;
    for(TActorIterator<ASenderCableCarRoute> It(GetWorld());It;++It)
    {
        if(Route) { UE_LOG(LogSenderCableCar,Error,TEXT("Multiple cable car routes found; keep one route in this world.")); return; }
        Route=*It;
    }
    if(!Route || !Route->RouteSpline || !Route->RouteSpline->IsClosedLoop())
    {
        UE_LOG(LogSenderCableCar,Error,TEXT("Missing closed CableCar_Route spline. Rail meshes are not used for movement.")); return;
    }
    MovementSpline=Route->RouteSpline;
    TotalLength=MovementSpline->GetSplineLength();
    if(TotalLength<100.f) { MovementSpline=nullptr; return; }
    SplineStartDistance=MovementSpline->GetDistanceAlongSplineAtLocation(InitialOrigin,ESplineCoordinateSpace::World);
    const FVector Projection=MovementSpline->GetLocationAtDistanceAlongSpline(SplineStartDistance,ESplineCoordinateSpace::World);
    SplineOffset=InitialOrigin-Projection;
    if(HangDrop!=0.f) SplineOffset.Z=-HangDrop;
    const FVector Marker=StopPoint->GetComponentsBoundingBox(true).GetCenter()-SplineOffset;
    const float AbsoluteStop=MovementSpline->GetDistanceAlongSplineAtLocation(Marker,ESplineCoordinateSpace::World);
    StopDistance=FMath::Fmod(AbsoluteStop-SplineStartDistance+TotalLength,TotalLength);
    InitialPathYaw=PathYaw(0.f);
    UE_LOG(LogSenderCableCar,Log,TEXT("Spline v3 ready: route=%s points=%d length=%.1f stop=%.1f dwell=%.2fs mesh-independent=true"),
        *Route->GetName(),MovementSpline->GetNumberOfSplinePoints(),TotalLength,StopDistance,StationWaitSeconds);
}
TStatId USenderCableCarSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(USenderCableCarSubsystem,STATGROUP_Tickables);
}
bool USenderCableCarSubsystem::IsInsideCar(const APawn* Pawn) const
{
    if(!Pawn || !CarInterior.IsValid) return false;
    const FVector P=CurrentRotation.UnrotateVector(Pawn->GetActorLocation()-CarOrigin);
    return CarInterior.IsInsideOrOn(P);
}
void USenderCableCarSubsystem::Tick(float DT)
{
    Super::Tick(DT);
    if(!bBuilt || !GetWorld()) return;
    if(State==ECableCarState::Waiting)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            const APlayerController* PC=It->Get();
            if(PC && IsInsideCar(PC->GetPawn()))
            {
                UE_LOG(LogSenderCableCar,Log,TEXT("Loop v2 boarded: %s time=%.3f"),*PC->GetPawn()->GetName(),GetWorld()->GetTimeSeconds()); StartRide(); break;
            }
        }
    }
    else if(State==ECableCarState::Arrived)
    {
        WaitRemaining-=DT;
        if(WaitRemaining<=0.f)
        {
            if(!bHeadingHome) Travelled=0.f;
            State=ECableCarState::Riding; CurrentSpeed=0.f;
            UE_LOG(LogSenderCableCar,Log,TEXT("Loop v2 depart: headingHome=%d time=%.3f"),bHeadingHome,GetWorld()->GetTimeSeconds());
        }
    }
    else MoveCar(DT);
}
void USenderCableCarSubsystem::StartRide()
{
    if(!bBuilt || State!=ECableCarState::Waiting) return;
    State=ECableCarState::Riding; CurrentSpeed=0.f; bHeadingHome=false;
}
void USenderCableCarSubsystem::ResetRide()
{
    if(!bOffsetsCaptured) return;
    PlaceCarAt(0.f);
    Travelled=CurrentSpeed=WaitRemaining=0.f; bHeadingHome=false; CompletedLoops=0;
    State=ECableCarState::Waiting;
}
void USenderCableCarSubsystem::MoveCar(float DT)
{
    const float Target=bHeadingHome?TotalLength:StopDistance;
    const float Remain=FMath::Max(0.f,Target-Travelled);
    const float Limit=FMath::Min(Speed,FMath::Sqrt(2.f*FMath::Max(Acceleration,1.f)*Remain));
    CurrentSpeed=FMath::FInterpConstantTo(CurrentSpeed,Limit,DT,FMath::Max(Acceleration,1.f));
    Travelled=FMath::Min(Target,Travelled+FMath::Max(CurrentSpeed,10.f)*DT);
    PlaceCarAt(Travelled);
    if(Travelled>=Target)
    {
        const bool Home=bHeadingHome;
        if(Home) ++CompletedLoops;
        bHeadingHome=!Home; CurrentSpeed=0.f;
        State=ECableCarState::Arrived; WaitRemaining=FMath::Max(StationWaitSeconds,0.f);
        UE_LOG(LogSenderCableCar,Log,TEXT("Loop v2 arrived: station=%s time=%.3f dwell=%.2f loops=%d distance=%.1f"),Home?TEXT("home"):TEXT("sensepoint2"),GetWorld()->GetTimeSeconds(),WaitRemaining,CompletedLoops,Travelled);
    }
}
float USenderCableCarSubsystem::PathYaw(float D) const
{
    if(!MovementSpline || TotalLength<=0.f) return 0.f;
    const float At=FMath::Fmod(SplineStartDistance+D+TotalLength,TotalLength);
    FVector Direction=MovementSpline->GetDirectionAtDistanceAlongSpline(At,ESplineCoordinateSpace::World);
    Direction.Z=0.; return Direction.Rotation().Yaw;
}
void USenderCableCarSubsystem::PlaceCarAt(float D)
{
    const FVector NewOrigin=EvaluatePath(D);
    const FQuat Q(FRotator(0.f,FMath::FindDeltaAngleDegrees(InitialPathYaw,PathYaw(D)),0.f));
    TArray<APawn*> Riders;
    if(bCarryRider)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        {
            APlayerController* PC=It->Get(); APawn* P=PC?PC->GetPawn():nullptr;
            if(IsInsideCar(P)) Riders.Add(P);
        }
    }
    for(int32 I=0;I<CarActors.Num();++I)
    {
        if(!IsValid(CarActors[I])) continue;
        const FTransform& T=CarStartTransforms[I];
        CarActors[I]->SetActorLocationAndRotation(NewOrigin+Q.RotateVector(T.GetLocation()-InitialOrigin),Q*T.GetRotation(),false,nullptr,ETeleportType::None);
    }
    for(APawn* P:Riders)
    {
        // CharacterMovement already carries a character based on a moving car component.
        const ACharacter* C=Cast<ACharacter>(P);
        const UPrimitiveComponent* Base=C?C->GetMovementBase():nullptr;
        if(Base && CarActors.Contains(Base->GetOwner())) continue;
        const FVector Local=CurrentRotation.UnrotateVector(P->GetActorLocation()-CarOrigin);
        const FQuat Delta=Q*CurrentRotation.Inverse();
        P->SetActorLocationAndRotation(NewOrigin+Q.RotateVector(Local),Delta*P->GetActorQuat(),false,nullptr,ETeleportType::TeleportPhysics);
        if(AController* Controller=P->GetController())
        {
            FRotator R=Controller->GetControlRotation(); R.Yaw+=Delta.Rotator().Yaw; Controller->SetControlRotation(R);
        }
    }
    CarOrigin=LastCarOrigin=NewOrigin; CurrentRotation=Q;
}
FVector USenderCableCarSubsystem::EvaluatePath(float D) const
{
    if(!MovementSpline || TotalLength<=0.f) return CarOrigin;
    const float At=FMath::Fmod(SplineStartDistance+D+TotalLength,TotalLength);
    return MovementSpline->GetLocationAtDistanceAlongSpline(At,ESplineCoordinateSpace::World)+SplineOffset;
}
