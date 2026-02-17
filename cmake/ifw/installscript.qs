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

    } else if (systemInfo.productType === "osx") {
        // Set the install folder icon to the app icon
        component.addOperation("Execute",
            "osascript", "-e",
            "use framework \"AppKit\"\n" +
            "set iconImage to (current application's NSImage's alloc()'s " +
                "initWithContentsOfFile:\"@TargetDir@/nexus-forge.app/Contents/Resources/appicon.icns\")\n" +
            "current application's NSWorkspace's sharedWorkspace()'s " +
                "setIcon:iconImage forFile:\"@TargetDir@\" options:0",
            "UNDOEXECUTE",
            "osascript", "-e",
            "use framework \"AppKit\"\n" +
            "current application's NSWorkspace's sharedWorkspace()'s " +
                "setIcon:(missing value) forFile:\"@TargetDir@\" options:0");
    }
}
