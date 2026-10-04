[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Write-Output "Read-only USB identity check (no device commands are sent)."

$usbDevices = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId.Contains("VID_4C4A&PID_8057") -or
    $_.FriendlyName -match "WL8[02].*UBOOT"
}

if (-not $usbDevices) {
    Write-Output "No matching WL82 UBOOT device is currently enumerated."
} else {
    foreach ($device in $usbDevices) {
        [pscustomobject]@{
            Class        = $device.Class
            Name         = $device.FriendlyName
            Status       = $device.Status
            InstanceId   = $device.InstanceId
        } | Format-List

        Get-PnpDeviceProperty -InstanceId $device.InstanceId -ErrorAction SilentlyContinue |
            Where-Object {
                $_.KeyName -in @(
                    "DEVPKEY_Device_BusReportedDeviceDesc",
                    "DEVPKEY_Device_HardwareIds",
                    "DEVPKEY_Device_Service",
                    "DEVPKEY_Device_Parent"
                )
            } |
            Select-Object KeyName, Data |
            Format-List
    }
}

Write-Output "Matching disk-interface records:"
$diskDevices = Get-PnpDevice -PresentOnly -Class DiskDrive | Where-Object {
    $_.FriendlyName -match "WL8[02].*UBOOT" -or
    $_.InstanceId -match "VEN_WL8[02].*PROD_UBOOT"
}
if ($diskDevices) {
    $diskDevices | Select-Object Class, FriendlyName, Status, InstanceId | Format-List
} else {
    Write-Output "None."
}

Write-Output "Matching serial-port records:"
$serialDevices = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue | Where-Object {
    $_.InstanceId.Contains("VID_4C4A&PID_8057") -or
    $_.FriendlyName -match "WL8[02]"
}
if ($serialDevices) {
    $serialDevices | Select-Object Class, FriendlyName, Status, InstanceId | Format-List
} else {
    Write-Output "None (the current bootloader enumeration is not exposing a matching COM port)."
}

$sdkTools = Join-Path $PSScriptRoot "..\..\references\source\fw-AC79_AIoT_SDK\cpu\wl82\tools"
$sdkTools = [System.IO.Path]::GetFullPath($sdkTools)
Write-Output "WL82 SDK tools directory: $sdkTools"
foreach ($toolName in @("isd_download.exe", "SM01-DFU.exe", "SM01-DFU_1.6.exe")) {
    $toolPath = Join-Path $sdkTools $toolName
    [pscustomobject]@{
        Tool   = $toolName
        Exists = Test-Path -LiteralPath $toolPath -PathType Leaf
        Path   = $toolPath
    }
}
