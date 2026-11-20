# Compila shaders GLSL 450 con glslc -O del NDK, los valida con spirv-val (vulkan1.1) y genera
# cabeceras C++ con el SPIR-V embebido (mismo formato que gfx/PresentSpv.h).
# Uso: powershell -File tools/build_shaders.ps1
$ErrorActionPreference = "Stop"
$tools = "C:\Users\Murdoc\AppData\Local\Android\Sdk\ndk\28.2.13676358\shader-tools\windows-x86_64"
$glslc = Join-Path $tools "glslc.exe"
$val = Join-Path $tools "spirv-val.exe"
$gfx = Join-Path $PSScriptRoot "..\app\src\main\cpp\gfx"
$shaders = Join-Path $gfx "shaders"
$tmp = Join-Path $env:TEMP "hyperplay_spv"
New-Item -ItemType Directory -Force $tmp | Out-Null

# Cabecera -> lista de (archivo GLSL, nombre del array)
$headers = [ordered]@{
    "PresentSgsrSpv.h" = @(, @("present_sgsr.frag", "kPresentSgsrFragSpv"))
    "HyperFrameGenSpv.h" = @(
        @("fg_pyramid.comp", "kFgPyramidCompSpv"),
        @("fg_flow.comp", "kFgFlowCompSpv"),
        @("fg_interp.comp", "kFgInterpCompSpv")
    )
}

foreach ($h in $headers.Keys) {
    $sb = New-Object System.Text.StringBuilder
    $srcs = ($headers[$h] | ForEach-Object { $_[0] }) -join ", "
    [void]$sb.AppendLine("#pragma once")
    [void]$sb.AppendLine("// Generado con glslc -O (NDK shader-tools) desde gfx/shaders/{$srcs}; validado con")
    [void]$sb.AppendLine("// spirv-val --target-env vulkan1.1. No editar a mano: tools/build_shaders.ps1.")
    [void]$sb.AppendLine("#include <cstdint>")
    [void]$sb.AppendLine("namespace hyperplay::gpu {")
    foreach ($entry in $headers[$h]) {
        $src = Join-Path $shaders $entry[0]
        if (-not (Test-Path $src)) { Write-Host "omitido (no existe): $($entry[0])"; continue }
        $spv = Join-Path $tmp ($entry[0] + ".spv")
        & $glslc -O --target-env=vulkan1.1 -o $spv $src
        if ($LASTEXITCODE -ne 0) { throw "glslc falló: $($entry[0])" }
        & $val --target-env vulkan1.1 $spv
        if ($LASTEXITCODE -ne 0) { throw "spirv-val falló: $($entry[0])" }
        $bytes = [IO.File]::ReadAllBytes($spv)
        $words = New-Object System.Collections.Generic.List[string]
        for ($i = 0; $i -lt $bytes.Length; $i += 4) { $words.Add(("0x{0:x8}u" -f [BitConverter]::ToUInt32($bytes, $i))) }
        [void]$sb.AppendLine("inline constexpr uint32_t $($entry[1])[] = {")
        for ($i = 0; $i -lt $words.Count; $i += 8) {
            $n = [Math]::Min(8, $words.Count - $i)
            [void]$sb.AppendLine("    " + (($words.GetRange($i, $n)) -join ", ") + ",")
        }
        [void]$sb.AppendLine("};")
        Write-Host ("{0}: {1} bytes, spirv-val OK" -f $entry[0], $bytes.Length)
    }
    [void]$sb.AppendLine("}  // namespace hyperplay::gpu")
    [IO.File]::WriteAllText((Join-Path $gfx $h), $sb.ToString().Replace("`r`n", "`n"), (New-Object System.Text.UTF8Encoding $false))
}
