# Empaqueta el runtime .NET de HyperPlay (ver README.md) en un zip con rutas '/' (ZipInputStream
# de Android no admite '\', que es lo que escribe Compress-Archive en PowerShell 5.1).
param(
    [Parameter(Mandatory = $true)][string]$RuntimeDir,   # carpeta 'mono' (bin, lib, etc, fna)
    [Parameter(Mandatory = $true)][string]$FnaDir,       # clon de FNA (para licenses/)
    [Parameter(Mandatory = $true)][string]$OutZip
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path $OutZip) { Remove-Item $OutZip }
$zip = [System.IO.Compression.ZipFile]::Open($OutZip, "Create")
try {
    $root = (Resolve-Path $RuntimeDir).Path.TrimEnd('\')
    Get-ChildItem $root -Recurse -File | ForEach-Object {
        $rel = "mono/" + $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, $rel, "Optimal")
    }
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, "$PSScriptRoot\THIRD_PARTY.txt", "mono/THIRD_PARTY.txt", "Optimal")
    Get-ChildItem "$FnaDir\licenses" -File | ForEach-Object {
        $name = if ($_.Name -eq "LICENSE") { "FNA.LICENSE" } else { $_.Name }
        [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $_.FullName, "mono/licenses/$name", "Optimal")
    }
} finally { $zip.Dispose() }
$h = (Get-FileHash $OutZip -Algorithm SHA256).Hash.ToLower()
"{0}  {1:N1} MB  sha256={2}" -f $OutZip, ((Get-Item $OutZip).Length / 1MB), $h
