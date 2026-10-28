using UnrealBuildTool;

public class sender : ModuleRules
{
    public sender(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "UMG", "Slate", "SlateCore" });

        // 车流撞人要把玩家血条清零，需要直接读写 GAS 的 Health 属性
        PublicDependencyModuleNames.AddRange(new[] { "GameplayAbilities", "GameplayTags", "GameplayTasks" });

        // Editor-only dependencies for the mesh merge helper (USenderMeshMerge).
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
                "MeshMergeUtilities",
                "MeshDescription",
                "StaticMeshDescription",
                "AssetRegistry",
                "UnrealEd"
            });
        }
    }
}

