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
            "@StartMenuDir@/Uninstall Nexus Forge.lnk",
            "workingDirectory=@TargetDir@",
            "description=Uninstall or update Nexus Forge");

    } else if (systemInfo.productType === "osx" || systemInfo.productType === "macos") {
        // Create a symlink in /Applications so the app appears directly in Launchpad/Finder
        // The symlink inherits the .app bundle icon automatically
        component.addOperation("Execute",
            "ln", "-sf",
            "@TargetDir@/nexus-forge.app",
            "@ApplicationsDir@/Nexus Forge.app",
            "UNDOEXECUTE",
            "rm", "-f", "@ApplicationsDir@/Nexus Forge.app");
    }
}
