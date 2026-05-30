// JoypadInput.Build.cs
//
// Builds the Joypad Input plugin module. libjoypad sources are compiled
// directly into the plugin via PrivateIncludePaths / explicit file listing.
// HIDAPI is consumed as a system dependency (Windows: vcpkg or prebuilt;
// macOS: brew install hidapi; Linux: libhidapi-dev).

using UnrealBuildTool;
using System.IO;

public class JoypadInput : ModuleRules
{
    public JoypadInput(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "InputDevice",
            "ApplicationCore",
            "Slate",
            "SlateCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Projects",
        });

        // --- libjoypad sources (vendored in for the example) -------------------
        // The plugin compiles libjoypad's C sources alongside its own C++ TUs.
        // Path resolves relative to this Build.cs: ../../../../../  → repo root,
        // then src/lib/libjoypad.
        string LibjoypadRoot = Path.GetFullPath(Path.Combine(ModuleDirectory,
            "..", "..", "..", "..", ".."));

        PublicIncludePaths.Add(Path.Combine(LibjoypadRoot, "include"));

        // Manually list .c sources we need. As the libjoypad driver tree grows,
        // additional parsers go here (or switch to a glob if you prefer).
        string LibjoypadSrc = Path.Combine(LibjoypadRoot, "src");
        PrivateIncludePaths.Add(LibjoypadSrc);
        // UBT compiles any .c/.cpp in the module tree; we symlink the libjoypad
        // sources via a wrapper translation unit below (libjoypad_unity.c) to
        // avoid scattering source paths.

        // --- HIDAPI -----------------------------------------------------------
        // Cross-platform USB HID transport. Plug-in users supply their own
        // HIDAPI install per platform. For windows you'd ship .lib/.dll
        // under ThirdParty/HIDAPI/Win64/ and add them here. macOS/Linux can
        // link the system libhidapi via PublicSystemLibraries.

        if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicSystemLibraries.Add("hidapi");
            PublicSystemIncludePaths.Add("/opt/homebrew/include");
            PublicSystemIncludePaths.Add("/opt/homebrew/include/hidapi");
            PublicSystemLibraryPaths.Add("/opt/homebrew/lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicSystemLibraries.Add("hidapi-hidraw");
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Drop hidapi.lib + hidapi.dll under ThirdParty/HIDAPI/Win64/
            // and uncomment:
            // string Hid = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "HIDAPI", "Win64");
            // PublicSystemIncludePaths.Add(Path.Combine(Hid, "include"));
            // PublicAdditionalLibraries.Add(Path.Combine(Hid, "hidapi.lib"));
            // RuntimeDependencies.Add("$(BinaryOutputDir)/hidapi.dll", Path.Combine(Hid, "hidapi.dll"));
        }

        bEnableExceptions = false;
        bUseRTTI = false;
    }
}
