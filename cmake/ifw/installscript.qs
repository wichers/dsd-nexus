// Qt Installer Framework - install script for Nexus Forge
// Creates platform-specific shortcuts and integrations

function Component()
{
    // No-op constructor
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        // Start Menu shortcut for Nexus Forge
        component.addOperation("CreateShortcut",
            "@TargetDir@/bin/nexus-forge.exe",
            "@StartMenuDir@/Nexus Forge.lnk",
            "workingDirectory=@TargetDir@/bin",
            "iconPath=@TargetDir@/bin/nexus-forge.exe",
            "iconId=0",
            "description=DSD Audio Toolkit");

        // Start Menu shortcut for Maintenance Tool (uninstall/update)
        component.addOperation("CreateShortcut",
            "@TargetDir@/maintenancetool.exe",
            "@StartMenuDir@/Uninstall DSD Nexus.lnk",
            "workingDirectory=@TargetDir@",
            "description=Uninstall or update DSD Nexus");

    } else if (systemInfo.productType === "osx" || systemInfo.productType === "macos") {
        // Move the .app bundle to /Applications as a real app (not a symlink)
        // IFW undoes operations in reverse order during uninstall/update
        component.addOperation("Execute",
            "mv", "@TargetDir@/nexus-forge.app", "@ApplicationsDir@/Nexus Forge.app",
            "UNDOEXECUTE",
            "mv", "@ApplicationsDir@/Nexus Forge.app", "@TargetDir@/nexus-forge.app");

        // Hide the install folder (CLI tools, maintenance tool) from Finder
        component.addOperation("Execute",
            "chflags", "hidden", "@TargetDir@",
            "UNDOEXECUTE",
            "chflags", "nohidden", "@TargetDir@");
    }
}
