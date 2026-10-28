#include "SenderTrafficSystem.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "Components/BoxComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderTraffic, Log, All);

ASenderTrafficSystem::ASenderTrafficSystem()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	SetRootComponent(Path);
	Path->SetMobility(EComponentMobility::Movable);
	Path->ClearSplinePoints(false);
	Path->AddSplinePoint(FVector(0.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
	Path->AddSplinePoint(FVector(5000.f, 0.f, 0.f), ESplineCoordinateSpace::Local, false);
	Path->UpdateSpline();

	// 默认左右各一条车道
	LaneOffsets.Add(-150.f);
	LaneOffsets.Add(150.f);

	// 默认车型：7_Cars_Pack 里的三款常见车，找不到就留空（面板里可手动指定）
	static ConstructorHelpers::FClassFinder<AActor> SedanBP(TEXT("/Game/7_Cars_Pack/Sedan_Car/Sedan_Car_Static"));
	if (SedanBP.Succeeded())
	{
		VehicleClasses.AddUnique(SedanBP.Class);
	}
	static ConstructorHelpers::FClassFinder<AActor> HatchbackBP(TEXT("/Game/7_Cars_Pack/Hatchback_Car/Hatchback_Car_Static"));
	if (HatchbackBP.Succeeded())
	{
		VehicleClasses.AddUnique(HatchbackBP.Class);
	}
	static ConstructorHelpers::FClassFinder<AActor> MinivanBP(TEXT("/Game/7_Cars_Pack/Minivan_Car/Minivan_Car_Static"));
	if (MinivanBP.Succeeded())
	{
		VehicleClasses.AddUnique(MinivanBP.Class);
	}
}

void ASenderTrafficSystem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (Path)
	{
		Path->UpdateSpline();
		BuildPathFromMarkers();
		CachedPathLength = FMath::Max(Path->GetSplineLength(), 1.f);
	}
}

// 沿方向打一条水平射线，求它穿过包围盒的进出距离
static bool RaySlabDistances(const FVector& Origin, const FVector& Dir, const FBox& Box, double& OutEnter, double& OutExit)
{
	OutEnter = -TNumericLimits<double>::Max();
	OutExit = TNumericLimits<double>::Max();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const double O = Origin[Axis];
		const double D = Dir[Axis];
		const double Lo = Box.Min[Axis];
		const double Hi = Box.Max[Axis];
		if (FMath::Abs(D) < 1e-6)
		{
			if (O < Lo || O > Hi)
			{
				return false;
			}
			continue;
		}
		double T1 = (Lo - O) / D;
		double T2 = (Hi - O) / D;
		if (T1 > T2)
		{
			Swap(T1, T2);
		}
		OutEnter = FMath::Max(OutEnter, T1);
		OutExit = FMath::Min(OutExit, T2);
		if (OutEnter > OutExit)
		{
			return false;
		}
	}
	return true;
}

bool ASenderTrafficSystem::ProbeRoadCorridor(const FVector& Seed, float& OutSurfaceZ, float& OutYMin, float& OutYMax) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	FCollisionQueryParams Params(TEXT("SenderTrafficProbe"), false);
	Params.AddIgnoredActor(this);
	if (IsValid(StartMarker))
	{
		Params.AddIgnoredActor(StartMarker.Get());
	}
	if (IsValid(EndMarker))
	{
		Params.AddIgnoredActor(EndMarker.Get());
	}

	const float ProbeTop = Seed.Z + 900.f;
	const float ProbeBottom = Seed.Z - 900.f;

	// 竖直打一条线，返回命中的路面高度；打不到返回一个很小的值
	auto TraceSurfaceZ = [&](float X, float Y) -> float
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, ProbeTop), FVector(X, Y, ProbeBottom),
			ECC_Visibility, Params))
		{
			return static_cast<float>(Hit.ImpactPoint.Z);
		}
		return -100000.f;
	};

	const float CenterZ = TraceSurfaceZ(Seed.X, Seed.Y);
	if (CenterZ < -1000.f)
	{
		return false;
	}

	// 沿 Y 向两侧扫，找出同一高度上连续的那一段路面（碰到护栏/墙就会断开）
	const float Tolerance = 45.f;
	float MinY = Seed.Y;
	float MaxY = Seed.Y;
	for (float Offset = 10.f; Offset <= 1200.f; Offset += 10.f)
	{
		const float Z = TraceSurfaceZ(Seed.X, Seed.Y + Offset);
		if (Z < -1000.f || FMath::Abs(Z - CenterZ) > Tolerance)
		{
			break;
		}
		MaxY = Seed.Y + Offset;
	}
	for (float Offset = 10.f; Offset <= 1200.f; Offset += 10.f)
	{
		const float Z = TraceSurfaceZ(Seed.X, Seed.Y - Offset);
		if (Z < -1000.f || FMath::Abs(Z - CenterZ) > Tolerance)
		{
			break;
		}
		MinY = Seed.Y - Offset;
	}

	if ((MaxY - MinY) < 400.f)
	{
		return false;
	}

	OutSurfaceZ = CenterZ;
	OutYMin = MinY;
	OutYMax = MaxY;
	return true;
}

float ASenderTrafficSystem::MeasureBottomOffset(AActor* Vehicle) const
{
	float Lowest = 0.f;
	bool bFound = false;
	if (!Vehicle)
	{
		return 0.f;
	}

	TArray<UStaticMeshComponent*> Meshes;
	Vehicle->GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->IsVisible() || !Mesh->GetStaticMesh())
		{
			continue;
		}
		// 组件 Bounds 是网格在世界的紧包围盒，比 GetActorBounds 准确
		const float Bottom = static_cast<float>(Mesh->Bounds.GetBox().Min.Z);
		if (!bFound || Bottom < Lowest)
		{
			Lowest = Bottom;
			bFound = true;
		}
	}

	return bFound ? (Lowest - static_cast<float>(Vehicle->GetActorLocation().Z)) : 0.f;
}

void ASenderTrafficSystem::ConfigureVehicle(AActor* Vehicle, bool bEnsureHitBox)
{
	if (!Vehicle)
	{
		return;
	}

	// 车蓝图默认是 Static，运行时移动必须改成 Movable
	TArray<UPrimitiveComponent*> Prims;
	Vehicle->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim)
		{
			continue;
		}
		// 触发盒自成一体，不能被这里改成实体
		if (Prim->GetFName() == FName(TEXT("TrafficHitBox")))
		{
			continue;
		}
		Prim->SetMobility(EComponentMobility::Movable);
		Prim->SetCanEverAffectNavigation(false);
		// 车身不参与刚体模拟，只作为一个"骑着路径平移的实体墙"
		Prim->SetSimulatePhysics(false);

		if (bSolidVehicle)
		{
			// 实体：玩家会被车真实挡住 / 推开
			Prim->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Prim->SetCollisionObjectType(ECC_WorldDynamic);
			Prim->SetCollisionResponseToAllChannels(ECR_Block);
			// 相机和视线的检测不要被车身挡（否则准星/镜头会顿一下）
			Prim->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			Prim->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			Prim->SetGenerateOverlapEvents(false);
		}
		else
		{
			// 穿透：只做判定，不推挤玩家
			Prim->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Prim->SetCollisionResponseToAllChannels(ECR_Ignore);
			Prim->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
			Prim->SetGenerateOverlapEvents(true);
		}
	}

	if (!bEnsureHitBox)
	{
		return;
	}

	// 触发盒比车身大一圈：实体车身会先把玩家挡在车外表面，靠这个膨胀量保证"贴上就判定"
	const FVector TriggerExtent = HitBoxExtent + FVector(FMath::Max(HitBoxPadding, 0.f));

	// 已经有撞击盒（重复 RestartTraffic 时）就只刷新尺寸，不再重复挂事件
	TArray<UBoxComponent*> Boxes;
	Vehicle->GetComponents<UBoxComponent>(Boxes);
	for (UBoxComponent* Box : Boxes)
	{
		if (Box && Box->GetFName() == FName(TEXT("TrafficHitBox")))
		{
			Box->SetBoxExtent(TriggerExtent);
			Box->SetRelativeLocation(FVector(0.f, 0.f, HitBoxExtent.Z));
			return;
		}
	}

	// 单独挂一个规整的撞击盒，尺寸和车一致，判定稳定
	UBoxComponent* HitBox = NewObject<UBoxComponent>(Vehicle, TEXT("TrafficHitBox"));
	if (HitBox)
	{
		HitBox->SetMobility(EComponentMobility::Movable);
		HitBox->SetupAttachment(Vehicle->GetRootComponent());
		HitBox->RegisterComponent();
		HitBox->SetBoxExtent(TriggerExtent);
		HitBox->SetRelativeLocation(FVector(0.f, 0.f, HitBoxExtent.Z));
		HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		HitBox->SetCollisionObjectType(ECC_WorldDynamic);
		HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
		HitBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		HitBox->SetGenerateOverlapEvents(true);
		HitBox->SetCanEverAffectNavigation(false);
		HitBox->OnComponentBeginOverlap.AddDynamic(this, &ASenderTrafficSystem::HandleVehicleOverlap);
	}
}

float ASenderTrafficSystem::GetLaneShiftForIndex(int32 Index) const
{
	const int32 LaneCount = LaneOffsets.Num();
	float LaneShift = (LaneCount > 0 ? LaneOffsets[Index % LaneCount] : 0.f) + LaneOffset;

	if (bClampToRoad && RoadYMax > RoadYMin)
	{
		// 车道不能把车推到路肩外：按车宽把偏移夹进路面
		const float HalfWidth = FMath::Max(HitBoxExtent.Y, 50.f);
		const float Margin = FMath::Max(RoadEdgeMargin, 0.f);
		const float Lo = RoadYMin + HalfWidth + Margin - RoadCenterY;
		const float Hi = RoadYMax - HalfWidth - Margin - RoadCenterY;
		if (Lo <= Hi)
		{
			LaneShift = FMath::Clamp(LaneShift, Lo, Hi);
		}
	}

	return LaneShift;
}

AActor* ASenderTrafficSystem::FindActorByLabel(const TCHAR* Label) const
{
	UWorld* World = GetWorld();
	if (!World || !Label)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!IsValid(Candidate) || Candidate == this)
		{
			continue;
		}
#if WITH_EDITOR
		if (Candidate->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
		{
			return Candidate;
		}
#endif
		if (Candidate->GetName().Equals(Label, ESearchCase::IgnoreCase))
		{
			return Candidate;
		}
	}
	return nullptr;
}

bool ASenderTrafficSystem::BuildPathFromMarkers()
{
	if (!Path)
	{
		return false;
	}

	AActor* Start = IsValid(StartMarker) ? StartMarker.Get() : FindActorByLabel(TEXT("start"));
	AActor* End = IsValid(EndMarker) ? EndMarker.Get() : FindActorByLabel(TEXT("end"));
	AActor* Road = IsValid(RoadActor) ? RoadActor.Get() : FindActorByLabel(TEXT("road"));

	if (!Start)
	{
		return false;
	}

	// 行驶方向：start -> end（只取水平分量）
	FVector Dir(1.f, 0.f, 0.f);
	if (End)
	{
		FVector Delta = End->GetActorLocation() - Start->GetActorLocation();
		Delta.Z = 0.f;
		if (!Delta.IsNearlyZero())
		{
			Dir = Delta.GetSafeNormal();
		}
	}
	else if (Path->GetNumberOfSplinePoints() >= 2)
	{
		FVector Delta = Path->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World)
			- Path->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		Delta.Z = 0.f;
		if (!Delta.IsNearlyZero())
		{
			Dir = Delta.GetSafeNormal();
		}
	}

	// 路面包围盒：用来把车流夹在路面上，不允许开出路面
	FBox RoadBox(ForceInit);
	if (bClampToRoad && Road)
	{
		// 注意：UE 5.8 的 GetActorBounds 返回的 Origin 就是包围盒中心，Extent 是半尺寸，
		// 直接 Center ± Extent 即可。（之前误把 Origin 当成 max 角又减了一次，导致中心偏了）
		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Road->GetActorBounds(false, Origin, Extent, false);
		if (Extent.GetAbsMax() > 1.0)
		{
			RoadBox = FBox(Origin - Extent, Origin + Extent);
			UE_LOG(LogSenderTraffic, Log,
				TEXT("[Traffic] road box min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) center=(%.0f,%.0f,%.0f)"),
				RoadBox.Min.X, RoadBox.Min.Y, RoadBox.Min.Z,
				RoadBox.Max.X, RoadBox.Max.Y, RoadBox.Max.Z,
				Origin.X, Origin.Y, Origin.Z);
		}
	}

	const float HalfLength = FMath::Max(HitBoxExtent.X, 50.f);
	const float Margin = FMath::Max(RoadEdgeMargin, 0.f);

	FVector P0 = Start->GetActorLocation();

	// 默认值：用路面包围盒推算
	RoadCenterY = RoadBox.IsValid ? static_cast<float>(RoadBox.GetCenter().Y) : static_cast<float>(P0.Y);
	RoadYMin = RoadBox.IsValid ? static_cast<float>(RoadBox.Min.Y) : 0.f;
	RoadYMax = RoadBox.IsValid ? static_cast<float>(RoadBox.Max.Y) : 0.f;
	RoadSurfaceZ = RoadBox.IsValid ? static_cast<float>(RoadBox.Max.Z) : static_cast<float>(P0.Z);

	// 运行时再用射线实测一次路面：能拿到真实的路面顶面和左右可行范围
	// （road 这个 actor 的包围盒 Y 方向只盖到车行道，右侧还有一段硬路肩）
	if (UWorld* ProbeWorld = GetWorld())
	{
		if (ProbeWorld->IsGameWorld() || ProbeWorld->WorldType == EWorldType::PIE)
		{
			FVector Seed = P0 + Dir * 900.f;
			float ProbedZ = RoadSurfaceZ;
			float ProbedYMin = RoadYMin;
			float ProbedYMax = RoadYMax;
			if (ProbeRoadCorridor(Seed, ProbedZ, ProbedYMin, ProbedYMax))
			{
				RoadSurfaceZ = ProbedZ;
				RoadYMin = ProbedYMin;
				RoadYMax = ProbedYMax;
				RoadCenterY = (ProbedYMin + ProbedYMax) * 0.5f;
				UE_LOG(LogSenderTraffic, Log,
					TEXT("[Traffic] measured road: surface_z=%.1f y=[%.0f..%.0f] center=%.0f"),
					RoadSurfaceZ, RoadYMin, RoadYMax, RoadCenterY);
			}
		}
	}

	// 车道整体摆在路面正中，靠某一侧时用 LaneOffset 调
	P0.Y = RoadCenterY;
	// 车底落在路面顶 + 间隙（各车型 pivot 不在车底时，Spawn 后会按网格包围盒再对齐一次）
	P0.Z = RoadSurfaceZ + FMath::Max(GroundClearance, 0.f);

	float MaxDist = 100000.f;
	if (RoadBox.IsValid)
	{
		// 用盒中心高度打水平射线，避免起点贴在盒顶面导致退化
		const FVector RayOrigin(P0.X, P0.Y, RoadBox.GetCenter().Z);
		double TEnter = 0.0;
		double TExit = 0.0;
		if (RaySlabDistances(RayOrigin, Dir, RoadBox, TEnter, TExit))
		{
			const double EnterClamped = FMath::Max(TEnter, 0.0);
			if (TEnter > 0.0)
			{
				// start 落在路面外，推进到进入点
				P0 += Dir * TEnter;
			}
			const double Available = FMath::Max(TExit - EnterClamped, 0.0);
			// 车头到边界即止，保证整辆车都在路面内
			MaxDist = static_cast<float>(Available) - HalfLength - Margin;
			if (MaxDist < 100.f)
			{
				MaxDist = static_cast<float>(Available);
			}
		}
		else
		{
			UE_LOG(LogSenderTraffic, Warning, TEXT("[Traffic] 行驶方向与路面不相交，退回原 Spline"));
			return false;
		}
	}
	else if (Path->GetSplineLength() > 1.f)
	{
		MaxDist = Path->GetSplineLength();
	}

	// 到 end 方块前就回收，别让车头怼进 end 里
	if (End)
	{
		const float EndDistance = static_cast<float>(FVector::DotProduct(End->GetActorLocation() - P0, Dir));
		if (EndDistance > 100.f)
		{
			MaxDist = FMath::Min(MaxDist, EndDistance - HalfLength - Margin);
		}
	}
	MaxDist = FMath::Max(MaxDist, 100.f);

	const FVector P1 = P0 + Dir * MaxDist;

	Path->ClearSplinePoints(false);
	Path->AddSplinePoint(P0, ESplineCoordinateSpace::World, false);
	Path->AddSplinePoint(P1, ESplineCoordinateSpace::World, false);
	Path->UpdateSpline();

	// 路径已经按 start -> end 排好，正向行驶即可
	bReverseDirection = false;
	CachedPathLength = FMath::Max(Path->GetSplineLength(), 1.f);

	UE_LOG(LogSenderTraffic, Log,
		TEXT("[Traffic] path rebuilt: start=%s end=%s road=%s | P0=(%.0f,%.0f,%.0f) P1=(%.0f,%.0f,%.0f) len=%.0fcm"),
		*GetNameSafe(Start), *GetNameSafe(End), *GetNameSafe(Road),
		P0.X, P0.Y, P0.Z, P1.X, P1.Y, P1.Z, CachedPathLength);
	return true;
}

void ASenderTrafficSystem::BeginPlay()
{
	Super::BeginPlay();

	BuildPathFromMarkers();

	if (Path)
	{
		CachedPathLength = FMath::Max(Path->GetSplineLength(), 1.f);
	}
	else
	{
		CachedPathLength = 1000.f;
	}

	SpawnVehicles();
}

void ASenderTrafficSystem::RestartTraffic()
{
	DestroyVehicles();
	SpawnVehicles();
}

void ASenderTrafficSystem::DestroyVehicles()
{
	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		AActor* Vehicle = Vehicles[Index];
		if (!IsValid(Vehicle))
		{
			continue;
		}
		// 关卡里原本摆好的车只是被拉进车流，不能销毁
		const bool bAdopted = AdoptedVehicleFlags.IsValidIndex(Index) && AdoptedVehicleFlags[Index];
		if (!bAdopted)
		{
			Vehicle->Destroy();
		}
	}
	Vehicles.Reset();
	TravelDistances.Reset();
	VehicleSpeeds.Reset();
	VehicleLaneShifts.Reset();
	VehicleBottomOffsets.Reset();
	AdoptedVehicleFlags.Reset();
}

void ASenderTrafficSystem::SpawnVehicles()
{
	UWorld* World = GetWorld();
	if (!World || !Path)
	{
		return;
	}

	// 车型池为空时退回到 Sedan，避免静默什么都不生成
	TArray<TSubclassOf<AActor>> Pool = VehicleClasses;
	if (Pool.Num() == 0)
	{
		if (UClass* Fallback = LoadClass<AActor>(nullptr, TEXT("/Game/7_Cars_Pack/Sedan_Car/Sedan_Car_Static.Sedan_Car_Static_C")))
		{
			Pool.Add(Fallback);
		}
	}

	// 关卡里已经摆好的车先占名额，不够的部分才新生成
	TArray<AActor*> Queue;
	for (TObjectPtr<AActor>& Preset : AdoptedVehicles)
	{
		if (IsValid(Preset))
		{
			Queue.Add(Preset.Get());
		}
	}

	if (Pool.Num() == 0 && Queue.Num() == 0)
	{
		UE_LOG(LogSenderTraffic, Warning, TEXT("No vehicle class available, traffic will be empty."));
		return;
	}

	const int32 Count = FMath::Max(VehicleCount, 1);
	const float Spacing = CachedPathLength / static_cast<float>(Count);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		float StartDistance = Spacing * static_cast<float>(Index);
		if (bReverseDirection)
		{
			StartDistance = CachedPathLength - StartDistance;
			if (StartDistance < 0.f)
			{
				StartDistance += CachedPathLength;
			}
		}

		const FVector SpawnLoc = Path->GetLocationAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::World);
		const FRotator SpawnRot = Path->GetRotationAtDistanceAlongSpline(StartDistance, ESplineCoordinateSpace::World);

		AActor* Vehicle = nullptr;
		bool bAdopted = false;

		if (Queue.IsValidIndex(Index))
		{
			Vehicle = Queue[Index];
			bAdopted = true;
			Vehicle->SetOwner(this);
		}
		else
		{
			if (Pool.Num() == 0)
			{
				continue;
			}
			TSubclassOf<AActor> ChosenClass = Pool[FMath::RandRange(0, Pool.Num() - 1)];
			if (!ChosenClass)
			{
				continue;
			}

			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.Owner = this;

			Vehicle = World->SpawnActor<AActor>(ChosenClass, SpawnLoc, SpawnRot, Params);
			if (!Vehicle)
			{
				continue;
			}
		}

		ConfigureVehicle(Vehicle, true);

		const float Range = FMath::Clamp(SpeedVariation, 0.f, 0.5f);
		const float ThisSpeed = Speed * FMath::FRandRange(1.f - Range, 1.f + Range);

		// 轮流分配到各条车道，让车流在路面上左右都有
		const float LaneShift = GetLaneShiftForIndex(Index);

		Vehicles.Add(Vehicle);
		TravelDistances.Add(StartDistance);
		VehicleSpeeds.Add(ThisSpeed);
		VehicleLaneShifts.Add(LaneShift);
		AdoptedVehicleFlags.Add(bAdopted);

		// 先把车摆到位，再按网格实际包围盒量 pivot 到车底的偏移
		UpdateVehicleTransform(Vehicle, StartDistance, 0.f, LaneShift);
		const float BottomOffset = MeasureBottomOffset(Vehicle);
		VehicleBottomOffsets.Add(BottomOffset);

		UpdateVehicleTransform(Vehicle, StartDistance, BottomOffset, LaneShift);
	}

	UE_LOG(LogSenderTraffic, Log,
		TEXT("Spawned %d traffic vehicles (preset %d), path %.0fcm, speed %.0fcm/s, surface_z=%.1f, lane_y=%.0f"),
		Vehicles.Num(), Queue.Num(), CachedPathLength, Speed, RoadSurfaceZ, RoadCenterY);
}

void ASenderTrafficSystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Path || Vehicles.Num() == 0 || CachedPathLength <= 1.f)
	{
		return;
	}

	const float DirectionSign = bReverseDirection ? -1.f : 1.f;

	for (int32 Index = 0; Index < Vehicles.Num(); ++Index)
	{
		AActor* Vehicle = Vehicles[Index];
		if (!IsValid(Vehicle))
		{
			continue;
		}

		float& Distance = TravelDistances[Index];
		const float ThisSpeed = VehicleSpeeds.IsValidIndex(Index) ? VehicleSpeeds[Index] : Speed;

		Distance += ThisSpeed * DirectionSign * DeltaSeconds;

		// 环绕
		if (Distance > CachedPathLength)
		{
			Distance -= CachedPathLength;
		}
		else if (Distance < 0.f)
		{
			Distance += CachedPathLength;
		}

		const float LaneShift = VehicleLaneShifts.IsValidIndex(Index) ? VehicleLaneShifts[Index] : 0.f;
		const float BottomOffset = VehicleBottomOffsets.IsValidIndex(Index) ? VehicleBottomOffsets[Index] : 0.f;
		UpdateVehicleTransform(Vehicle, Distance, BottomOffset, LaneShift);
	}
}

void ASenderTrafficSystem::UpdateVehicleTransform(AActor* Vehicle, float Distance, float BottomOffset, float LaneShift)
{
	if (!Vehicle || !Path)
	{
		return;
	}

	const FVector BaseLoc = Path->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	FRotator Rot = Path->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

	// 车底高度：路面顶 + 间隙，并且不低于 MinVehicleBottomZ（轮胎不许低于这个高度）
	const float SurfaceZ = FMath::Max(RoadSurfaceZ + FMath::Max(GroundClearance, 0.f), MinVehicleBottomZ);
	const float PivotZ = SurfaceZ - BottomOffset + VehicleZOffset;

	// 沿路径右方向做车道偏移
	const FVector RightDir = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	FVector FinalLoc(BaseLoc.X + RightDir.X * LaneShift, BaseLoc.Y + RightDir.Y * LaneShift, PivotZ);

	if (bReverseDirection)
	{
		Rot.Yaw += 180.f;
	}

	Vehicle->SetActorLocationAndRotation(FinalLoc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
}

void ASenderTrafficSystem::HandleVehicleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bFailOnHit || !OtherActor)
	{
		return;
	}

	// 只认玩家，AI / 僵尸不参与判定
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	AActor* Vehicle = OverlappedComp ? OverlappedComp->GetOwner() : nullptr;
	TriggerPlayerHit(Vehicle, OtherActor);
}

void ASenderTrafficSystem::TriggerPlayerHit(AActor* Vehicle, AActor* Player)
{
	if (!Player)
	{
		return;
	}

	UE_LOG(LogSenderTraffic, Warning, TEXT("[Traffic] Player hit by %s"), *GetNameSafe(Vehicle));

	OnPlayerHit.Broadcast(Vehicle, Player);

	// 先把血条清零，再走伤害流程：这样即便伤害流程里没有任何反应，血条也是空的
	if (bZeroPlayerHealth)
	{
		ZeroPlayerHealth(Player);
	}

	if (bApplyDamage)
	{
		UGameplayStatics::ApplyDamage(Player, HitDamage, nullptr, Vehicle, nullptr);
	}

	if (bRestartLevelOnHit && !bHitAlreadyTriggered)
	{
		bHitAlreadyTriggered = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(RestartTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, true);
				UGameplayStatics::OpenLevel(this, FName(*LevelName));
			}), FMath::Max(RestartDelay, 0.01f), false);
		}
	}
}

bool ASenderTrafficSystem::ZeroPlayerHealth(AActor* Player)
{
	if (!Player)
	{
		return false;
	}

	// 玩家的血量走 GAS（IWALS_BaseAttributeSet 的 Health 属性），
	// 把属性基础值写成 0，绑定了这个属性的血条 UI 会立刻变空。
	UAbilitySystemComponent* ASC = nullptr;
	if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Player))
	{
		ASC = ASI->GetAbilitySystemComponent();
	}
	if (!ASC)
	{
		TArray<UAbilitySystemComponent*> Found;
		Player->GetComponents<UAbilitySystemComponent>(Found);
		if (Found.Num() > 0)
		{
			ASC = Found[0];
		}
	}
	if (!ASC)
	{
		UE_LOG(LogSenderTraffic, Warning,
			TEXT("[Traffic] %s 没有 AbilitySystemComponent，无法清零血条"), *GetNameSafe(Player));
		return false;
	}

	int32 Zeroed = 0;
	for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
	{
		if (!Set)
		{
			continue;
		}
		for (TFieldIterator<FStructProperty> It(Set->GetClass()); It; ++It)
		{
			FStructProperty* Prop = *It;
			if (!Prop || Prop->Struct != FGameplayAttributeData::StaticStruct())
			{
				continue;
			}
			const FString PropName = Prop->GetName();
			// 只清 Health / CurrentHealth，MaxHealth 这类上限值不动
			if (!PropName.Contains(TEXT("Health")) || PropName.Contains(TEXT("Max")))
			{
				continue;
			}
			const FGameplayAttribute Attr(Prop);
			ASC->SetNumericAttributeBase(Attr, 0.f);
			++Zeroed;
			UE_LOG(LogSenderTraffic, Log,
				TEXT("[Traffic] 血条清零：%s.%s -> 0"), *GetNameSafe(Set), *PropName);
		}
	}

	return Zeroed > 0;
}
