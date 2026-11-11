param(
    [string]$Ova = (Join-Path $PSScriptRoot 'Green2-Core26.ova'),
    [Parameter(Mandatory=$true)][string]$Destination,
    [ValidatePattern('^[a-z_][a-z0-9_-]{0,31}$')][string]$Username = 'green2',
    [Security.SecureString]$Password,
    [string]$OvfTool = "$env:ProgramFiles\VMware\VMware Workstation\OVFTool\ovftool.exe",
    [switch]$Start
)
$ErrorActionPreference = 'Stop'
if ($Username -eq 'root') { throw 'Choose a non-root username.' }
if (!(Test-Path -LiteralPath $Ova)) { throw 'OVA file not found.' }
if (!(Test-Path -LiteralPath $OvfTool)) { throw 'Set -OvfTool to the precompiled VMware OVF Tool.' }
if ((Test-Path -LiteralPath $Destination) -and
    (Get-ChildItem -LiteralPath $Destination -Force | Select-Object -First 1)) {
    throw 'Destination must be new or empty.'
}
if (!$Password) { $Password = Read-Host 'Initial login password (at least 8 characters)' -AsSecureString }
$pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($Password)
try {
    $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer)
    if ($plain.Length -lt 8 -or $plain.Length -gt 256 -or $plain.IndexOfAny([char[]]@(0,10,13)) -ge 0) {
        throw 'Password must contain 8 to 256 characters and no line breaks.'
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $vmx = Join-Path (Resolve-Path -LiteralPath $Destination) 'Green2-Core26.vmx'
    # Password is never passed on the OVF Tool command line.
    & $OvfTool --acceptAllEulas --allowExtraConfig $Ova $vmx
    if ($LASTEXITCODE -ne 0) { throw 'OVF import failed.' }
    function VmxValue([string]$value) { return $value.Replace('|','|7C').Replace('"','|22') }
    $text = [IO.File]::ReadAllText($vmx)
    $keys = @('guestinfo.green2.username','guestinfo.green2.password','ethernet0.connectionType')
    foreach ($key in $keys) {
        $text = [regex]::Replace($text, '(?m)^' + [regex]::Escape($key) + '\s*=.*\r?\n?', '')
    }
    $text += "`nguestinfo.green2.username = `"$(VmxValue $Username)`"`n"
    $text += "guestinfo.green2.password = `"$(VmxValue $plain)`"`n"
    $text += "ethernet0.connectionType = `"nat`"`n"
    [IO.File]::WriteAllText($vmx, $text, [Text.UTF8Encoding]::new($false))
    Write-Output "Imported VM: $vmx"
    Write-Output "First-boot username: $Username; password omitted."
    if ($Start) {
        $vmrun = Join-Path (Split-Path (Split-Path $OvfTool)) 'vmrun.exe'
        & $vmrun start $vmx nogui
        if ($LASTEXITCODE -ne 0) { throw 'VM start failed.' }
    }
} finally {
    $plain = $null
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer)
}
