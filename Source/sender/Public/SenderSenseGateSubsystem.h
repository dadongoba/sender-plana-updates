// 感应门（Sense Gate）：
//   玩家碰到 sensepoint 方块时，parclose 方块消失（隐藏 + 关碰撞）；
//   玩家离开后 parclose 重新出现（可配置成一次性触发）。
//
// 识别方式（两种都支持，取并集）：
//   1. Actor 标签：sensepoint / parclose
//   2. 材质名包含关键字：sensepoint / parclose（方便以后直接涂材质就生效）
//
// 约束：
//   - 只认持久关卡（PersistentLevel）里的 Actor，LevelInstance 内部的同名材质物体
//     （例如迁移关卡里的两辆劳斯莱斯）不会误触发。
//   - 判定用「包围盒相交」而不是碰撞回调，所以不用给方块开 Generate Overlap Events，
//     静态挡板也能正常判定接触。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderSenseGateSubsystem.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Sense Gate"))
class SENDER_API USenderSenseGateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 感应方块（踩上去触发）的 Actor 标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	FName SenseActorTag = TEXT("sensepoint");

	/** 被感应控制的方块（消失的隔断）的 Actor 标签。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	FName TargetActorTag = TEXT("parclose");

	/** 材质名关键字识别：sensepoint。留空则只按标签识别。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	FString SenseMaterialKeyword = TEXT("sensepoint");

	/** 材质名关键字识别：parclose。留空则只按标签识别。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	FString TargetMaterialKeyword = TEXT("parclose");

	/** 感应盒向外扩张，让玩家站在板子边缘也算碰到。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	float SensePadding = 15.f;

	/**
	 * false（默认）= 踩上去一次后，本次游玩内一直消失；重新开始 Play 会恢复原状。
	 * true = 玩家一离开就重新出现。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	bool bRestoreWhenReleased = false;

	/** 检测间隔（秒）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|SenseGate")
	float CheckInterval = 0.05f;

	/** 立即重新检测一次（调试 / 蓝图调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|SenseGate")
	void ForceEvaluate();

	/** 当前是否处于"被踩住"状态。 */
	UFUNCTION(BlueprintPure, Category = "Sender|SenseGate")
	bool IsTriggered() const { return bTriggered; }

private:
	void CollectActors();
	void Evaluate();
	void SetTargetsHidden(bool bHide);

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SenseActors;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> TargetActors;

	/** 记录隔断原始状态，恢复时用。 */
	TMap<TObjectPtr<AActor>, bool> TargetOriginalCollision;

	FTimerHandle CheckHandle;
	bool bTriggered = false;
};
