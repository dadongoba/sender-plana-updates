// 场景里的 fragment 方块（31cm 小方块，散落在场景各处）的"幽灵化"逻辑：
//   1. 开始前隐藏
//   2. 点击开始执行（PIE Play / 或手动调用 StartFragmentPhase）后全部出现
//   3. 执行期间没有实体：碰撞改为 QueryOnly + 只对 Pawn 做 Overlap，玩家可直接穿过
//   4. 玩家一接触就消失（隐藏或销毁，可配置）
//
// 目标方块优先按 Tag（默认 "Fragment"）查找，找不到时按材质名兜底，避免漏掉后来复制出来的方块。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderFragmentSubsystem.generated.h"

/** 所有碎片方块都被接触清除时触发 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSenderFragmentsAllCleared, int32, TotalCleared);

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Fragment Blocks"))
class SENDER_API USenderFragmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 进入执行阶段：所有碎片出现，变为无阻挡的幽灵状态，玩家接触即消失。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Fragment")
	void StartFragmentPhase();

	/** 回到开始前状态：全部隐藏、关闭碰撞，可反复调用以便重开一轮。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Fragment")
	void ResetFragmentPhase();

	UFUNCTION(BlueprintPure, Category = "Sender|Fragment")
	int32 GetTotalBlocks() const { return TotalBlocks; }

	UFUNCTION(BlueprintPure, Category = "Sender|Fragment")
	int32 GetRemainingBlocks() const { return RemainingBlocks; }

	UFUNCTION(BlueprintPure, Category = "Sender|Fragment")
	bool IsPhaseActive() const { return bPhaseActive; }

	/** 全部清除完毕 */
	UPROPERTY(BlueprintAssignable, Category = "Sender|Fragment")
	FSenderFragmentsAllCleared OnAllFragmentsCleared;

protected:
	UFUNCTION()
	void HandleBlockBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void CollectBlocks();
	void ApplyGhostState(AActor* Block);
	void ClearBlock(AActor* Block);
	void SetBlockSolidNone(AActor* Block, bool bInHidden);

	/** 点 Play 后就自动进入执行阶段 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	bool bAutoStartOnBeginPlay = true;

	/** 开始前先把方块藏起来，等执行阶段才出现 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	bool bHideBeforeStart = true;

	/** 自动开始的延迟（秒），留一点点时间让场景加载完 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	float StartDelay = 0.1f;

	/** 被玩家碰到时直接销毁方块；关掉则只是隐藏，方便 Reset 后重来 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	bool bDestroyOnTouch = false;

	/** 只有玩家能触发消失，AI/僵尸不会带走方块 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	bool bOnlyPlayerTriggers = true;

	/** 目标方块 Tag，留空则不按 Tag 查找 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	FName BlockTag = TEXT("Fragment");

	/** 材质名兜底关键字，留空则不做材质扫描 */
	UPROPERTY(Config, EditAnywhere, Category = "Fragment")
	FName MaterialKeyword = TEXT("fragment");

private:
	/** 收集到的方块（弱引用，防止方块被销毁后悬空） */
	TArray<TWeakObjectPtr<AActor>> Blocks;

	/** 已经被接触清除的方块 */
	TSet<AActor*> ClearedBlocks;

	FTimerHandle StartTimerHandle;

	int32 TotalBlocks = 0;
	int32 RemainingBlocks = 0;
	bool bPhaseActive = false;
};
