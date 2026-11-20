# Compila la suite nativa del JIT y el escÃ¡ner, y los sube al dispositivo.
param([string]$Only = "")
$ErrorActionPreference = "Continue"
$ndk = "C:\Users\Murdoc\AppData\Local\Android\Sdk\ndk\28.2.13676358\toolchains\llvm\prebuilt\windows-x86_64\bin\aarch64-linux-android31-clang++.cmd"
$src = "C:\Users\Murdoc\Desktop\Studio 820 Apps\HyperPlay\app\src\main\cpp"
$tst = "C:\Users\Murdoc\Desktop\Studio 820 Apps\HyperPlay\app\src\test\cpp"
$out = "$env:TEMP\opencode"; New-Item -ItemType Directory -Force $out | Out-Null
$adb = "C:\Users\Murdoc\AppData\Local\Android\Sdk\platform-tools\adb.exe"
$common = @("-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-static-libstdc++", "-I$src",
            "$src\cpu\OryonEmitterBackend.cpp", "$src\cpu\HyperAotCache.cpp", "$src\cpu\GuestProfiler.cpp",
            "$src\loader\PeHeaderInspector.cpp", "-llog")
$targets = @{ "oryon_test" = "$tst\OryonEmitterBackendTest.cpp"; "jit_scan" = "$tst\JitScan.cpp"; "jit_diff" = "$tst\JitDiff.cpp" }
foreach ($name in $targets.Keys) {
    if ($Only -and $Only -ne $name) { continue }
    & $ndk @($targets[$name]) @common -o "$out\$name"
    if ($LASTEXITCODE -ne 0) { throw "fallÃ³ $name" }
    & $adb -s 192.168.0.9:44517 push "$out\$name" "/data/local/tmp/$name" | Out-Null
    & $adb -s 192.168.0.9:44517 shell chmod 755 "/data/local/tmp/$name"
    "$name OK"
}
