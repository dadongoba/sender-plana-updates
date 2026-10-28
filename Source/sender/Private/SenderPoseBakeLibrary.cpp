#include "SenderPoseBakeLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Misc/FrameNumber.h"

DEFINE_LOG_CATEGORY_STATIC(LogSenderPoseBake, Log, All);

bool USenderPoseBakeLibrary::BakeStaticPose(UAnimSequence* Sequence,
	const TArray<FName>& BoneNames,
	const TArray<float>& Data,
	int32 NumFrames,
	bool bClearExisting)
{
	if (!Sequence)
	{
		UE_LOG(LogSenderPoseBake, Error, TEXT("BakeStaticPose: Sequence 为空"));
		return false;
	}
	if (BoneNames.Num() == 0 || Data.Num() != BoneNames.Num() * 10)
	{
		UE_LOG(LogSenderPoseBake, Error, TEXT("BakeStaticPose: 数据长度不匹配 (bones=%d, floats=%d)"),
			BoneNames.Num(), Data.Num());
		return false;
	}

	IAnimationDataModel* Model = Sequence->GetDataModel();
	if (!Model)
	{
		UE_LOG(LogSenderPoseBake, Error, TEXT("BakeStaticPose: 拿不到 DataModel"));
		return false;
	}

	TScriptInterface<IAnimationDataController> ControllerInterface = Model->GetController();
	IAnimationDataController* Controller = ControllerInterface.GetInterface();
	if (!Controller)
	{
		UE_LOG(LogSenderPoseBake, Error, TEXT("BakeStaticPose: 拿不到 DataController"));
		return false;
	}

	const int32 FrameCount = FMath::Max(2, NumFrames);

	Sequence->Modify();
	Controller->OpenBracket(NSLOCTEXT("SenderPoseBake", "BakePose", "烘焙姿势"), false);
	if (bClearExisting)
	{
		Controller->RemoveAllBoneTracks(false);
	}
	Controller->SetNumberOfFrames(FFrameNumber(FrameCount), false);

	int32 TrackCount = 0;
	for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
	{
		const float* Raw = &Data[Index * 10];
		const FVector Location(Raw[0], Raw[1], Raw[2]);
		FQuat Rotation(Raw[3], Raw[4], Raw[5], Raw[6]);
		Rotation.Normalize();
		const FVector Scale(Raw[7], Raw[8], Raw[9]);

		TArray<FVector> PositionKeys;
		TArray<FQuat> RotationKeys;
		TArray<FVector> ScaleKeys;
		PositionKeys.Init(Location, FrameCount);
		RotationKeys.Init(Rotation, FrameCount);
		ScaleKeys.Init(Scale, FrameCount);

		Controller->AddBoneCurve(BoneNames[Index], false);
		if (Controller->SetBoneTrackKeys(BoneNames[Index], PositionKeys, RotationKeys, ScaleKeys, false))
		{
			++TrackCount;
		}
	}

	Controller->CloseBracket(false);
	Sequence->PostEditChange();

	UE_LOG(LogSenderPoseBake, Log, TEXT("BakeStaticPose: 写入 %d/%d 条骨骼轨道，帧数 %d"),
		TrackCount, BoneNames.Num(), FrameCount);
	return TrackCount > 0;
}

bool USenderPoseBakeLibrary::BakeTalkingGesture(UAnimSequence* Sequence,UAnimSequence* Idle,int32 Style)
{
    if(!Sequence || !Idle || Sequence==Idle || Sequence->GetSkeleton()!=Idle->GetSkeleton()) return false;
    const IAnimationDataModel* Source=Idle->GetDataModel();
    if(!Source) return false;
    TArray<FName> Names;Source->GetBoneTrackNames(Names);
    TArray<FTransform> Base;Source->GetBoneTracksTransform(Names,FFrameNumber(0),Base);
    IAnimationDataModel* Model=Sequence->GetDataModel();if(!Model) return false;
    TScriptInterface<IAnimationDataController> C=Model->GetController();
    Sequence->Modify();C->OpenBracket(NSLOCTEXT("Sender","TalkBake","Bake Taro talking gesture"),false);
    C->SetFrameRate(FFrameRate(30,1),false);C->SetNumberOfFrames(FFrameNumber(96),false);
    bool Success=true;
    for(int32 B=0;B<Names.Num();++B)
    {
        TArray<FVector> P,S;TArray<FQuat> Q;
        const FString Bone=Names[B].ToString();
        for(int32 F=0;F<=96;++F)
        {
            const double T=F/96.;
            const double Envelope=FMath::Square(FMath::Sin(PI*T));
            const double Wave=Envelope*FMath::Sin(2.*PI*T);
            FRotator Add(0,0,0);
            if(Bone==TEXT("neck_01")) Add.Pitch=Style==0?-4.*Envelope:Style==2?3.*Envelope*FMath::Sin(4.*PI*T):1.5*Wave;
            if(Bone==TEXT("head")) {Add.Yaw=(Style==0?2.:1.)*Wave;Add.Pitch=Style==2?2.*Wave:0.;}
            if(Bone==TEXT("spine_03")) Add.Yaw=(Style==1?2.:.7)*Wave;
            if(Style==1 && Bone==TEXT("clavicle_r")) Add.Roll=2.*Envelope;
            if(Style==1 && Bone==TEXT("upperarm_r")) Add.Yaw=2.*Wave;
            P.Add(Base[B].GetLocation());S.Add(Base[B].GetScale3D());Q.Add((Base[B].GetRotation()*Add.Quaternion()).GetNormalized());
        }
        C->AddBoneCurve(Names[B],false);
        Success=C->SetBoneTrackKeys(Names[B],P,Q,S,false) && Success;
    }
    C->CloseBracket(false);Sequence->PostEditChange();
    UE_LOG(LogSenderPoseBake,Log,TEXT("Talking gesture baked: %s style=%d tracks=%d seamless 3.2s result=%d"),*Sequence->GetName(),Style,Names.Num(),Success);
    return Success;
}
