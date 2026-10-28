// Single-rail clockwise loop. Board once to start; dwell at both stations, then repeat.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderCableCarSubsystem.generated.h"

class AActor;
class APawn;
class USplineComponent;

UENUM(BlueprintType)
enum class ECableCarState : uint8
{
	Waiting,
	Riding,
	Arrived
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Cable Car"))
class SENDER_API USenderCableCarSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// ------------------------------------------------------------- 识别方式

	/** 轨道方块的 Actor 标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FName TrackTag = TEXT("cable_track");

	/** 车厢方块的 Actor 标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FName CarTag = TEXT("cable_car");

	/** 起点触发方块（sensepoint1）的标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FName StartTag = TEXT("cable_start");

	/** 终点方块（sensepoint2）的标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FName StopTag = TEXT("cable_stop");

	/** 轨道所在的世界大纲文件夹名（标签缺失时的兜底）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FString TrackFolder = TEXT("tram");

	/** 车厢所在的世界大纲文件夹名（标签缺失时的兜底）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FString CarFolder = TEXT("cablecar");

	/** 起点方块名字关键字（标签缺失时的兜底）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FString StartLabel = TEXT("sensepoint1");

	/** 终点方块名字关键字（标签缺失时的兜底）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FString StopLabel = TEXT("sensepoint2");

	/**
	 * 车厢锚点：整组车厢以这一块的位置为准，终点也按它的高度对齐 sensepoint2。
	 * 必须指向顶板（keyb），否则到站时整车会偏离轨道。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	FString CarRootLabel = TEXT("keyb");

	// ----------------------------------------------------------------- 运动

	/** 行进速度 cm/s。ALS 奔跑 600、冲刺 900，取中间偏慢。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar", meta = (ClampMin = "10.0"))
	float Speed = 450.f;

	/** 起步加速度 cm/s²，避免一开动就是全速。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar", meta = (ClampMin = "50.0"))
	float Acceleration = 700.f;

	/** 到站减速距离 cm，最后这段线速度收下来。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar", meta = (ClampMin = "0.0"))
	float DecelDistance = 350.f;

	/** 车厢比轨道梁中心低多少 cm。0 = 自动按起点位置推算。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	float HangDrop = 0.f;

	/** 两根轨道梁端点相距多近算接上了（cm）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar", meta = (ClampMin = "10.0"))
	float LinkTolerance = 420.f;

	/** 触发盒向外扩张，让玩家站在板子边缘也算碰到。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	float SensePadding = 15.f;

	/** 玩家站在车厢里时，跟着车厢一起走。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	bool bCarryRider = true;

	/** 旧配置兼容项；闭环运行不再需要再次触发。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar")
	bool bAllowRetrigger = false;

	/** 两个站点各自的停留时间（秒）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|CableCar", meta = (ClampMin = "0.0"))
	float StationWaitSeconds = 5.f;

	// ------------------------------------------------------------------ 状态

	UFUNCTION(BlueprintPure, Category = "Sender|CableCar")
	ECableCarState GetState() const { return State; }

	/** 已走里程 / 总里程，0~1。 */
	UFUNCTION(BlueprintPure, Category = "Sender|CableCar")
	float GetProgress() const { return TotalLength > 0.f ? FMath::Clamp(Travelled / TotalLength, 0.f, 1.f) : 0.f; }

	/** 手动放行（蓝图 / 调试用）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|CableCar")
	void StartRide();

	/** 立即回到起点并复位（调试用）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|CableCar")
	void ResetRide();

private:
	void CollectActors();
	void BuildPath();
	void ApplyInitialOffsets();
	void MoveCar(float DeltaTime);
	void PlaceCarAt(float InTravelled);
	FVector EvaluatePath(float InTravelled) const;
	float PathYaw(float Distance) const;
	bool IsInsideCar(const APawn* Pawn) const;


	bool ActorMatches(const AActor* Actor, FName Tag, const FString& Folder, const FString& Label) const;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> TrackActors;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> CarActors;

	UPROPERTY()
	TObjectPtr<AActor> StartPoint = nullptr;

	UPROPERTY()
	TObjectPtr<AActor> StopPoint = nullptr;

	/** 车厢每块的初始世界位置，用于复位。 */
	TArray<FVector> CarStartLocations;
	TArray<FTransform> CarStartTransforms;
	FBox CarInterior = FBox(ForceInit);
	FVector InitialOrigin = FVector::ZeroVector;
	FQuat CurrentRotation = FQuat::Identity;
	float InitialPathYaw = 0.f;
	float StopDistance = 0.f;
	float WaitRemaining = 0.f;
	bool bHeadingHome = false;
	int32 CompletedLoops = 0;

	/** 轨道折线（已在梁中心高度上减去 HangDrop）。 */
	TArray<FVector> PathPoints;

	UPROPERTY(Transient)
	TObjectPtr<USplineComponent> MovementSpline = nullptr;
	float SplineStartDistance = 0.f;
	FVector SplineOffset = FVector::ZeroVector;

	/** 每段长度与累计长度。 */
	TArray<float> SegmentLengths;
	TArray<float> CumulativeLengths;

	FVector CarOrigin = FVector::ZeroVector;
	FVector LastCarOrigin = FVector::ZeroVector;

	ECableCarState State = ECableCarState::Waiting;
	float Travelled = 0.f;
	float TotalLength = 0.f;
	float CurrentSpeed = 0.f;
	float ResolvedHangDrop = 0.f;
	bool bBuilt = false;
	bool bOffsetsCaptured = false;
};
