// 把一组骨骼的局部变换烘焙成一段静态 AnimSequence（编辑器期工具）。

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SenderPoseBakeLibrary.generated.h"

class UAnimSequence;

UCLASS()
class SENDER_API USenderPoseBakeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 把一个静态姿势写进 AnimSequence。
	 *
	 * @param Sequence       目标序列（必须是本骨架的 AnimSequence，且已通过 Factory 创建）
	 * @param BoneNames      骨骼名，顺序与 Data 对应
	 * @param Data           每根骨骼 10 个 float：Loc.XYZ、Quat.XYZW、Scale.XYZ
	 * @param NumFrames      序列帧数（>=2）
	 * @param bClearExisting 是否先清空已有骨骼轨道
	 * @return 是否写入成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Sender|Pose")
	static bool BakeStaticPose(UAnimSequence* Sequence,
		const TArray<FName>& BoneNames,
		const TArray<float>& Data,
		int32 NumFrames,
		bool bClearExisting);
    /** Create a seamless talking gesture from this character's own idle pose. */
    UFUNCTION(BlueprintCallable, Category="Sender|Pose")
    static bool BakeTalkingGesture(UAnimSequence* Sequence, UAnimSequence* Idle, int32 Style);
};
