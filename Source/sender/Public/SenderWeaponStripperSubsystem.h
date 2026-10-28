// 武器清理（Weapon Stripper）：
//   场景里的人物本身不带枪，枪是 ALS_Base_CharacterBP 在 BeginPlay 之后
//   动态 Spawn 出来挂到角色身上的（BP_I_Rifle_Actor_For_Player / BP_I_Pistol_Actor）。
//   所以关卡里删不掉，只能在运行时把它们移除。
//
// 做法：
//   1. 周期性扫描世界，把类名命中关键字的武器 Actor 直接销毁（含已挂到角色身上的）；
//   2. 顺手隐藏角色身上的占位组件（Rifle-SkeletalRoot / Pistol-Attach-Root 等），
//      这些是 SM_Cube / Cylinder 占位块，枪没了以后会露出来。
//
// 配置（Config/DefaultGame.ini 可覆盖）：
//   [/Script/sender.SenderWeaponStripperSubsystem]
//   bRemoveWeapons=True
//   +WeaponClassKeywords=Rifle_Actor
//   +WeaponClassKeywords=Pistol_Actor

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SenderWeaponStripperSubsystem.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Sender Weapon Stripper"))
class SENDER_API USenderWeaponStripperSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	USenderWeaponStripperSubsystem();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** 总开关。关掉后什么都不做。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Weapons")
	bool bRemoveWeapons = true;

	/** 类名包含任一关键字即视为武器 Actor，会被销毁。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Weapons")
	TArray<FString> WeaponClassKeywords;

	/** 角色身上需要隐藏的占位组件名关键字（SM_Cube / Cylinder 假枪）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Weapons")
	TArray<FString> HideComponentKeywords;

	/** 扫描间隔（秒）。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Weapons")
	float ScanInterval = 0.2f;

	/** 持续扫描多久（秒）。武器是在 BeginPlay 后陆续生成的，需要盯一段时间。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sender|Weapons")
	float ScanDuration = 15.f;

	/** 立刻执行一次清理（调试 / 蓝图调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Weapons")
	void StripNow();

	/** 已销毁的武器数量。 */
	UFUNCTION(BlueprintPure, Category = "Sender|Weapons")
	int32 GetRemovedCount() const { return RemovedCount; }

private:
	void StartScanning(UWorld* InWorld);
	void Scan();
	void HidePlaceholderComponents(AActor* Actor);
	bool IsWeaponActor(const AActor* Actor) const;

	FTimerHandle ScanHandle;
	float ElapsedSeconds = 0.f;
	int32 RemovedCount = 0;
	bool bFinished = false;
};
