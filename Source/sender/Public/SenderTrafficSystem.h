// 车流障碍物（需求表 R16 / 地图障碍系统）：
//   「车流在高架桥上对于角色逆向行驶，速度为 m/s。玩家与车流障碍物发生碰撞立刻判定失败」
//
// 实现：沿 Spline 循环行驶的车辆队列。
//   - 车辆用 7_Cars_Pack 里的静态车蓝图（Sedan / Hatchback / Minivan / Luxury / Cargo_Van / Crossover / Station_Wagon）
//   - 车身做成实体碰撞（bSolidVehicle），玩家会被车真实挡住 / 顶开，撞上车头会被推着走
//   - 每台车另外挂一个略大于车身的 BoxComponent 作为"撞击触发器"，
//     玩家一贴上就判定失败：把 GAS 的 Health 属性清零（血条瞬间空掉），再走一遍伤害流程
//   - 速度、数量、车道偏移、方向全部可在细节面板调

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SenderTrafficSystem.generated.h"

class USplineComponent;

/** 车流撞到玩家时广播（参数是被撞的那台车） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSenderTrafficPlayerHit, AActor*, Vehicle, AActor*, Player);

UCLASS(Blueprintable, meta = (DisplayName = "Sender Traffic System"))
class SENDER_API ASenderTrafficSystem : public AActor
{
	GENERATED_BODY()

public:
	ASenderTrafficSystem();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 行驶路径。默认沿 X 轴一条直线，把这个 Actor 摆到桥上后按桥的走向调两个样条点即可。
	 *  若场景里存在 start / end 标记，BeginPlay 时会被自动重建成 start -> end 方向的直线段。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Path")
	TObjectPtr<USplineComponent> Path;

	// ---------------- 路径标记 ----------------

	/** 车流出生点（场景里 label 为 start 的方块）。留空则运行时按名字自动找。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Path")
	TObjectPtr<AActor> StartMarker;

	/** 行驶目标点（label 为 end 的方块），只用来确定车头朝向与行驶方向。留空则自动找。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Path")
	TObjectPtr<AActor> EndMarker;

	/** 允许行驶的路面（label 为 road 的方块）。用它自己的包围盒把车流夹在路面上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Path")
	TObjectPtr<AActor> RoadActor;

	/** 是否把车流限制在路面包围盒内，防止车开出路面后悬空 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Path")
	bool bClampToRoad = true;

	/** 在车长/车宽之外，距离路面边缘再留的余量（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Path", meta = (ClampMin = "0"))
	float RoadEdgeMargin = 20.f;

	// ---------------- 车辆 ----------------

	/** 可随机挑选的车型池，留空则用默认 Sedan */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	TArray<TSubclassOf<AActor>> VehicleClasses;

	/** 同时在场上的车辆数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "1", ClampMax = "60"))
	int32 VehicleCount = 6;

	/** 车速（cm/s）。城市道路 40km/h ≈ 1100，60km/h ≈ 1670 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "50"))
	float Speed = 1200.f;

	/** 每台车速度随机浮动比例（0 = 全部同速） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0", ClampMax = "0.5"))
	float SpeedVariation = 0.12f;

	/** 逆向行驶：true = 沿 Spline 反方向开（默认，符合"对角色逆向"的需求） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	bool bReverseDirection = true;

	/** 车道横向偏移列表（cm），相对路面中心线；每台车按顺序轮流分配到这些车道上。
	 *  默认左右各一条，避免车流全挤在路的一侧。留空则退回 LaneOffset 单车道。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	TArray<float> LaneOffsets;

	/** 整体车道偏移（cm），在 LaneOffsets 之外再多挪一点，用来微调车流靠左还是靠右 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	float LaneOffset = 0.f;

	/** 车辆相对路径的高度偏移（cm），车 pivot 在车底时一般是 0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	float VehicleZOffset = 0.f;

	/** 车底与路面之间留的间隙（cm）。轮胎最底部会停在这个高度上 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle", meta = (ClampMin = "0"))
	float GroundClearance = 0.5f;

	/** 车底绝对高度下限（世界 Z，cm）。低于这个值会被强制抬起来，防止轮胎陷进路面 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	float MinVehicleBottomZ = 1410.f;

	/** 关卡里已经摆好的车（比如手动拖进来的 Sedan），运行时直接拉进车流一起跑，
	 *  占队伍名额，不再额外生成一台。留空则全部动态生成。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Vehicle")
	TArray<TObjectPtr<AActor>> AdoptedVehicles;

	/** 实测到的路面顶面高度（运行时自动填，可在细节面板查看） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Debug")
	float RoadSurfaceZ = 0.f;

	/** 实测到的可行路面 Y 起点（cm） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Debug")
	float RoadYMin = 0.f;

	/** 实测到的可行路面 Y 终点（cm） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Debug")
	float RoadYMax = 0.f;

	/** 车流中心线的 Y（路面正中） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Traffic|Debug")
	float RoadCenterY = 0.f;

	// ---------------- 撞击判定 ----------------

	/** 撞到玩家是否判定失败 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	bool bFailOnHit = true;

	/** 车身是否做成实体碰撞。true = 玩家会被车真实挡住 / 推开，撞上去有实体感；
	 *  false = 车身只做穿透式触发（车会从玩家身上穿过去）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	bool bSolidVehicle = true;

	/** 撞击触发盒在车身之外额外膨胀的尺寸（cm）。实体车身会先把玩家挡住，
	 *  触发盒比车身大一圈，才能保证"贴上车的瞬间"就判定，而不是被挡住后判不到。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit", meta = (ClampMin = "0"))
	float HitBoxPadding = 40.f;

	/** 撞击时把玩家血条直接清零（走 GAS：把 Health 属性写成 0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	bool bZeroPlayerHealth = true;

	/** 撞击盒半尺寸（cm）。默认按常见轿车 5.2m 长 x 2.2m 宽 x 1.8m 高 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	FVector HitBoxExtent = FVector(260.f, 110.f, 90.f);

	/** 撞击时对玩家施加的伤害（走项目的 GAS / Health 系统） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	bool bApplyDamage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit", meta = (EditCondition = "bApplyDamage"))
	float HitDamage = 10000.f;

	/** 撞击后直接重开当前关卡（项目里已有失败流程就关掉这个） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit")
	bool bRestartLevelOnHit = false;

	/** 撞击后延迟多久重开（秒），留点时间播撞击反馈 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Hit", meta = (EditCondition = "bRestartLevelOnHit"))
	float RestartDelay = 1.2f;

	UPROPERTY(BlueprintAssignable, Category = "Traffic")
	FSenderTrafficPlayerHit OnPlayerHit;

	// ---------------- 接口 ----------------

	/** 清掉现有车辆重新生成（改完参数可调用） */
	UFUNCTION(BlueprintCallable, Category = "Traffic")
	void RestartTraffic();

	UFUNCTION(BlueprintPure, Category = "Traffic")
	int32 GetSpawnedVehicleCount() const { return Vehicles.Num(); }

	UFUNCTION(BlueprintPure, Category = "Traffic")
	float GetPathLength() const { return CachedPathLength; }

protected:
	UFUNCTION()
	void HandleVehicleOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void SpawnVehicles();
	void DestroyVehicles();
	void UpdateVehicleTransform(AActor* Vehicle, float Distance, float BottomOffset = 0.f, float LaneShift = 0.f);
	void TriggerPlayerHit(AActor* Vehicle, AActor* Player);

	/** 把玩家的 GAS Health 属性归零，血条立刻空掉（不依赖具体血条 UI 的实现） */
	bool ZeroPlayerHealth(AActor* Player);

	/** 把车改成可移动 + 只做查询，并按需补一个撞击盒 */
	void ConfigureVehicle(AActor* Vehicle, bool bEnsureHitBox);

	/** 量车子上"最低的可见网格"到 pivot 的落差（cm，通常是负数），用来把轮胎对齐到路面 */
	float MeasureBottomOffset(AActor* Vehicle) const;

	/** 从种子点向两侧扫射线，求出路面顶面高度与可行 Y 范围 */
	bool ProbeRoadCorridor(const FVector& Seed, float& OutSurfaceZ, float& OutYMin, float& OutYMax) const;

	/** 第 Index 台车该用哪条车道（已按路面宽度夹过） */
	float GetLaneShiftForIndex(int32 Index) const;

	/** 按 start / end / road 标记重建行驶路径，返回是否重建成功 */
	bool BuildPathFromMarkers();

	/** 按 label 在关卡里找 actor（编辑器下比对 ActorLabel，运行时退回对象名） */
	AActor* FindActorByLabel(const TCHAR* Label) const;

	TArray<TObjectPtr<AActor>> Vehicles;
	/** 每台车当前沿路径走过的距离（cm） */
	TArray<float> TravelDistances;
	/** 每台车自己的速度（cm/s），带随机浮动 */
	TArray<float> VehicleSpeeds;
	/** 每台车所在车道的横向偏移（cm） */
	TArray<float> VehicleLaneShifts;
	/** 每台车 pivot 到车底的偏移（cm） */
	TArray<float> VehicleBottomOffsets;
	/** 这一台是不是关卡里原本摆好的车（是的话回收时不能销毁） */
	TArray<bool> AdoptedVehicleFlags;

	float CachedPathLength = 1000.f;
	bool bHitAlreadyTriggered = false;
	FTimerHandle RestartTimerHandle;
};
