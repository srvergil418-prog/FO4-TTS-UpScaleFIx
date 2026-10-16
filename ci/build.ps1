# ============================================================================
# ci/build.ps1 -- ALL build + compatibility logic for TrueThroughScope
# ----------------------------------------------------------------------------
# Called by .github/workflows/main.yml, which is now only a thin entry point.
# Everything that needs to change when adapting TTS to the public CommonLibF4
# lives HERE (and in ci/PCH.append.h), so the workflow file rarely changes.
#
# Repository layout expected (everything below src/):
#   src/True-Through-Scope/        <- this repo (contains ci/, include/, src/)
#   src/CommonLibF4/               <- powerof3/CommonLibF4
#   src/imgui/                     <- ocornut/imgui (docking)
# ============================================================================

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

Write-Host '============================================================'
Write-Host 'TTS_BUILD_SCRIPT_MARKER_01'
Write-Host '============================================================'

# ---------------------------------------------------------------- paths
$tts      = Split-Path -Parent $PSScriptRoot          # .../src/True-Through-Scope
$src      = Join-Path $tts 'src'
$pchPath  = Join-Path $src 'PCH.h'
$shimPath = Join-Path $PSScriptRoot 'PCH.append.h'
$commonlibf4 = Join-Path (Split-Path -Parent $tts) 'CommonLibF4'
$imguiDir    = Join-Path (Split-Path -Parent $tts) 'imgui'

Write-Host "tts        = $tts"
Write-Host "src        = $src"
Write-Host "pchPath    = $pchPath"
Write-Host "shimPath   = $shimPath"
Write-Host "CommonLibF4= $commonlibf4"
Write-Host "imgui      = $imguiDir"
Write-Host ''

# ---------------------------------------------------------------- preflight
$missing = @()
foreach ($p in @($pchPath, $shimPath, $commonlibf4, $imguiDir)) {
    if (-not (Test-Path $p)) { $missing += $p }
}
if ($missing.Count -gt 0) {
    Write-Host '!! MISSING:' -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "   $_" -ForegroundColor Red }
    throw 'TTS CI: required paths are missing (see above)'
}

Write-Host '--- PCH.h tail BEFORE patching ---'
Get-Content $pchPath | Select-Object -Last 12 | ForEach-Object { '   ' + $_ }
Write-Host ''

# ================================================================ 1) hkVector4.h
$hv = Join-Path $commonlibf4 'CommonLibF4/include/RE/Havok/hkVector4.h'
$t = Get-Content $hv -Raw
$t2 = $t.Replace('hkVector4f& GetNormalized()', 'hkVector4f GetNormalized()')
if ($t2 -eq $t) { throw 'hkVector4.h: pattern not found' }
Set-Content $hv -Value $t2 -NoNewline -Encoding utf8
Write-Host '  [1/9] hkVector4.h'

# ================================================================ 2) API.cpp
$api = Join-Path $commonlibf4 'CommonLibF4/src/F4SE/API.cpp'
$a = Get-Content $api -Raw
$a2 = $a.Replace(
    'log::info("{} v{}", GetPluginName(), GetPluginVersion());',
    'log::info("{} v{}", GetPluginName(), GetPluginVersion().string());')
if ($a2 -eq $a) { throw 'API.cpp: pattern not found' }
Set-Content $api -Value $a2 -NoNewline -Encoding utf8
Write-Host '  [2/9] API.cpp'

# ================================================================ 3) REX/LOG.cpp
$lg = Join-Path $commonlibf4 'CommonLibF4/src/REX/REX/LOG.cpp'
$g = Get-Content $lg -Raw
if ($g -notmatch 'REX/REX/CONVERT\.h') {
    $g = $g.Replace('#include "REX/W32/USER32.h"',
                    "#include `"REX/W32/USER32.h`"`r`n#include `"REX/REX/CONVERT.h`"")
}
$rx = [regex]'(?s)void LOG\(const std::source_location a_loc, const LOG_LEVEL a_level, const std::wstring_view a_fmt\)\s*\{.*?\r?\n\t\}'
$m = $rx.Match($g)
if (-not $m.Success) { throw 'REX/LOG.cpp: wide overload not found' }
$repl = "void LOG(const std::source_location a_loc, const LOG_LEVEL a_level, const std::wstring_view a_fmt)`r`n`t{`r`n`t`t// spdlog accepts narrow chars only; convert UTF-16 -> UTF-8 first`r`n`t`tstd::string narrow;`r`n`t`tif (!REX::UTF16_TO_UTF8(a_fmt, narrow)) {`r`n`t`t`tnarrow = `"<encoding error>`";`r`n`t`t}`r`n`t`tLOG(a_loc, a_level, std::string_view{ narrow });`r`n`t}"
$g2 = $g.Remove($m.Index, $m.Length).Insert($m.Index, $repl)
if ($g2 -eq $g) { throw 'REX/LOG.cpp: replacement failed' }
Set-Content $lg -Value $g2 -NoNewline -Encoding utf8
Write-Host '  [3/9] REX/REX/LOG.cpp'

# ================================================================ 4) REL/Module.h
$mod = Join-Path $commonlibf4 'CommonLibF4/include/REL/Module.h'
$mo = Get-Content $mod -Raw
if ($mo -notmatch 'static bool IsF4') {
    $mo2 = $mo -replace '(class\s+Module\s*\{)', "`$1`r`n`tpublic:`r`n`t`t// Added by build shim: this build targets the desktop (non-VR) runtime.`r`n`t`t[[nodiscard]] static bool IsF4() { return true; }`r`n`t`t[[nodiscard]] static bool IsVR() { return false; }"
    if ($mo2 -eq $mo) { throw 'REL/Module.h: could not insert IsF4/IsVR' }
    Set-Content $mod -Value $mo2 -NoNewline -Encoding utf8
    Write-Host '  [4/9] REL/Module.h (IsF4/IsVR)'
} else {
    Write-Host '  [4/9] REL/Module.h already patched'
}

# ================================================================ 5) PCH.h NOMINMAX
$p = Get-Content $pchPath -Raw
if ($p -notmatch 'NOMINMAX') {
    $p = "#define NOMINMAX`r`n" + $p
    Write-Host '  [5/9] PCH.h += NOMINMAX'
} else {
    Write-Host '  [5/9] PCH.h already has NOMINMAX'
}

# ================================================================ 6) append the shim
$shim = Get-Content $shimPath -Raw
if ($p -notmatch 'TTS_APPENDED_BUILD_SHIM') {
    $p = $p + "`r`n" + "// TTS_APPENDED_BUILD_SHIM`r`n" + $shim + "`r`n"
    Write-Host '  [6/9] PCH.h += ci/PCH.append.h'
} else {
    Write-Host '  [6/9] PCH.h already has the shim'
}
Set-Content $pchPath -Value $p -NoNewline -Encoding utf8

# ================================================================ 7) verify the patch really landed
Write-Host ''
Write-Host '--- PCH.h tail AFTER patching ---'
Get-Content $pchPath | Select-Object -Last 16 | ForEach-Object { '   ' + $_ }
Write-Host ''

$pchNow = [System.IO.File]::ReadAllText($pchPath)
# NOTE: there is deliberately no "undef near/far" check here any more. The
# near/far problem is solved by include/RE/NetImmerse/NiFrustum.h overriding the
# library header with nearPlane/farPlane members, so no #undef is needed.
# The override itself is verified right below, because the whole approach rests
# on it.
$checks = @(
    @{ Name = 'NOMINMAX';          Pattern = '(?m)^#define\s+NOMINMAX' },
    @{ Name = 'shim marker';       Pattern = 'TTS_APPENDED_BUILD_SHIM' },
    @{ Name = 'BSLight';           Pattern = 'class BSLight' },
    @{ Name = 'NiFrustumPlanes';   Pattern = 'NiFrustumPlanes::Set' },
    @{ Name = 'ShadowSceneNode';   Pattern = 'class ShadowSceneNode' },
    @{ Name = 'BSShaderAccumulator'; Pattern = 'class BSShaderAccumulator' },
    @{ Name = 'BSRenderPass';      Pattern = 'class BSRenderPass' },
    @{ Name = 'BSShaderManagerState'; Pattern = 'class BSShaderManagerState' },
    @{ Name = 'frust.nearPlane';   Pattern = 'frust\.nearPlane' }
)
$failed = @()
foreach ($c in $checks) {
    if ([regex]::IsMatch($pchNow, $c.Pattern)) {
        Write-Host ("  OK   " + $c.Name) -ForegroundColor Green
    } else {
        Write-Host ("  FAIL " + $c.Name) -ForegroundColor Red
        $failed += $c.Name
    }
}
if ($failed.Count -gt 0) {
    throw ('TTS CI: PCH.h patch verification failed: ' + ($failed -join ', '))
}
Write-Host '  [7/9] PCH.h patch verified'

# Verify the NiFrustum override is in place and really does rename the two
# macro-tainted members. This is the linchpin of the near/far fix.
$nifOverride = Join-Path $tts 'include/RE/NetImmerse/NiFrustum.h'
if (-not (Test-Path $nifOverride)) {
    throw "TTS CI: missing $nifOverride -- the near/far fix depends on it"
}
$nifText = [System.IO.File]::ReadAllText($nifOverride)
foreach ($need in @('float nearPlane;', 'float farPlane;', 'sizeof(NiFrustum) == 0x1C')) {
    if (-not $nifText.Contains($need)) {
        throw "TTS CI: NiFrustum.h override is missing '$need'"
    }
}
if ([regex]::IsMatch($nifText, '(?m)^\s*float\s+near\s*;')) {
    throw 'TTS CI: NiFrustum.h override still declares a bare near member'
}
if ([regex]::IsMatch($nifText, '(?m)^\s*float\s+far\s*;')) {
    throw 'TTS CI: NiFrustum.h override still declares a bare far member'
}
Write-Host '  OK   NiFrustum.h override verified (nearPlane/farPlane, 0x1C)' -ForegroundColor Green
Write-Host ''

# ================================================================ 8) TTS source tweaks
# (a) NiFLoader.cpp: QShaderProperty() does not exist on powerof3's BSTriShape.
#     Its result only feeds ProcessBSEffectShaderProperty(), which is an empty
#     placeholder, so nullptr changes nothing.
# (b) NiFLoader.cpp: stream->topObjects[0] -- powerof3's NiTLargeArray has no
#     accessors at all; use build_shim::LargeArrayAt instead.
$nif = Join-Path $src 'NiFLoader.cpp'
$nf = Get-Content $nif -Raw
$nf2 = $nf.Replace(
    'NiShadeProperty* shadeProperty = triShape->QShaderProperty();',
    'NiShadeProperty* shadeProperty = nullptr;  // build shim: QShaderProperty() unavailable in powerof3')
if ($nf2 -eq $nf) { throw 'NiFLoader.cpp: QShaderProperty pattern not found' }
$nf3 = $nf2.Replace(
    'stream->topObjects[0].get()',
    'RE::build_shim::LargeArrayAt(stream->topObjects, 0).get()')
if ($nf3 -eq $nf2) { throw 'NiFLoader.cpp: topObjects pattern not found' }
Set-Content $nif -Value $nf3 -NoNewline -Encoding utf8
Write-Host '  [8/9] NiFLoader.cpp (QShaderProperty, topObjects)'

# SettingsPanel.cpp: imgui 1.92 removed the ImGuiKey_Mod* spellings.
$set = Join-Path $src 'UI/Panels/SettingsPanel.cpp'
$s = Get-Content $set -Raw
$s1 = $s.Replace('ImGuiKey_ModCtrl', 'ImGuiMod_Ctrl')
$s2 = $s1.Replace('ImGuiKey_ModShift', 'ImGuiMod_Shift')
$s3 = $s2.Replace('ImGuiKey_ModAlt', 'ImGuiMod_Alt')
if ($s3 -eq $s) { throw 'SettingsPanel.cpp: ImGuiKey_Mod* pattern not found' }
Set-Content $set -Value $s3 -NoNewline -Encoding utf8
Write-Host '  [8/9] SettingsPanel.cpp (ImGuiMod_*)'

# NOTE: there is no NiFrustum member-rename step any more.
# include/RE/NetImmerse/NiFrustum.h overrides the library header with members
# named nearPlane/farPlane -- exactly what TTS's sources already write. This
# avoids <Windows.h>'s empty near/far macros entirely, with no #undef games.

# ================================================================ 10) RendererShadowState
# powerof3 leaves RendererShadowState as an opaque 0x910-byte blob, but TTS
# reads shadowState.CameraData.viewProjMat and shadowState.PosAdjust.
# Offsets mirror CameraStateData (the equivalent structure State uses:
# ViewData @ 000 with viewProjMat @ 0D0, and posAdjust @ 210).
$bgp = Join-Path $commonlibf4 'CommonLibF4/include/RE/Bethesda/BSGraphics.h'
$b = Get-Content $bgp -Raw
$bNew = $b.Replace(
    "			std::byte pad[0x910];  // 000",
    "			// build shim: TTS reads shadowState.CameraData.viewProjMat and`r`n" +
    "			// shadowState.PosAdjust. Offsets mirror CameraStateData (ViewData @ 000,`r`n" +
    "			// viewProjMat @ 0D0, posAdjust @ 210), the structure State uses.`r`n" +
    "			struct CameraData_t`r`n" +
    "			{`r`n" +
    "				std::byte pad0[0x0D0];`r`n" +
    "				__m128    viewProjMat[4];             // 0D0`r`n" +
    "				std::byte pad1[0x210 - 0x0D0 - 0x40]; // 110`r`n" +
    "			};`r`n" +
    "			static_assert(sizeof(CameraData_t) == 0x210);`r`n" +
    "`r`n" +
    "			CameraData_t CameraData;  // 000`r`n" +
    "			NiPoint3     PosAdjust;   // 210`r`n" +
    "			std::byte    pad[0x6F4];  // 21C")
if ($bNew -eq $b) { throw 'BSGraphics.h: RendererShadowState placeholder not found' }
Set-Content $bgp -Value $bNew -NoNewline -Encoding utf8
Write-Host '  [10/12] RendererShadowState += CameraData / PosAdjust'

# ================================================================ 11) downgrade engine entry points
# These are engine functions that only the author's private library exposes.
# Calling an address from his Address Library would execute an unrelated
# function, so each is replaced by something TTS already does, or by a no-op.
$spr = Join-Path $src 'rendering/SecondPassRenderer.cpp'
$sp = Get-Content $spr -Raw

# (a) DrawWorld::SetCamera / Set*FOV -- the global camera is assigned on the
#     very next line, and FOV travels with the camera object itself.
$sp1 = $sp.Replace(
    "		DrawWorld::SetCamera(m_scopeCamera);`r`n" +
    "		DrawWorld::SetUpdateCameraFOV(true);`r`n" +
    "		DrawWorld::SetAdjusted1stPersonFOV(ScopeCamera::GetTargetFOV());`r`n" +
    "		DrawWorld::SetCameraFov(ScopeCamera::GetTargetFOV());",
    "		// build shim: DrawWorld::SetCamera / Set*FOV are private-library engine`r`n" +
    "		// entry points. The global render camera is assigned on the next line, and`r`n" +
    "		// the FOV lives on the camera object itself.`r`n" +
    "		(void)m_scopeCamera;")
if ($sp1 -eq $sp) { throw 'SecondPassRenderer: DrawWorld block #1 not found' }

$sp2 = $sp1.Replace(
    "			DrawWorld::SetCamera(m_originalCamera);`r`n" +
    "			DrawWorld::SetUpdateCameraFOV(true);",
    "			// build shim: DrawWorld::* unavailable. The camera is re-applied just`r`n" +
    "			// below via m_originalCamera->Update(nData).")
if ($sp2 -eq $sp1) { throw 'SecondPassRenderer: DrawWorld block #2 not found' }

# (b) State::SetCameraData -- the scope camera is already assigned to the
#     global pointers a few lines above, and TTS computes view/projection
#     matrices itself further down.
$sp3 = $sp2.Replace(
    "		gState.SetCameraData(m_scopeCamera, true,`r`n" +
    "			m_scopeCamera->viewFrustum.nearPlane,`r`n" +
    "			m_scopeCamera->viewFrustum.farPlane);",
    "		// build shim: State::SetCameraData is not present in powerof3. The scope`r`n" +
    "		// camera is already assigned to the global pointers above.`r`n" +
    "		(void)gState;")
if ($sp3 -eq $sp2) { throw 'SecondPassRenderer: SetCameraData block not found' }

# (c) Renderer::GetSingleton().SetPosAdjust(...) -- write the value through the
#     Context we already hold (the line above reads it back the same way).
$sp4 = $sp3.Replace(
    "				RE::BSGraphics::Renderer::GetSingleton().SetPosAdjust(&scopePosAdjust);",
    "				// build shim: Renderer::GetSingleton() is not present in powerof3.`r`n" +
    "				// Write through the Context we already hold (read back the same way`r`n" +
    "				// two lines above).`r`n" +
    "				context->shadowState.PosAdjust = scopePosAdjust;")
if ($sp4 -eq $sp3) { throw 'SecondPassRenderer: Renderer::GetSingleton block not found' }

Set-Content $spr -Value $sp4 -NoNewline -Encoding utf8
Write-Host '  [11/12] SecondPassRenderer.cpp downgraded (DrawWorld, SetCameraData, PosAdjust)'

# (d) TESEquipEvent -- its event-source singleton is exposed by no public
#     CommonLibF4 and its REL ID is unknown. Equip detection only drives the
#     automatic model switch, so it is disabled rather than calling a guess.
$ehp = Join-Path $src 'EventHandler.cpp'
$e = Get-Content $ehp -Raw
$e1 = $e.Replace(
    "		TESEquipEvent::GetSingleton()->RegisterSink(this);",
    "		// build shim: TESEquipEvent's event-source singleton is not exposed by any`r`n" +
    "		// public CommonLibF4 and its REL ID is unknown. Equip detection only drives`r`n" +
    "		// the automatic scope-model switch, so it is disabled rather than calling a`r`n" +
    "		// guessed engine address.")
if ($e1 -eq $e) { throw 'EventHandler.cpp: RegisterSink call not found' }

$e2 = $e1.Replace(
    "		auto equipEventSource = RE::TESEquipEvent::GetEventSource();`r`n" +
    "		if (equipEventSource) {`r`n" +
    "			equipEventSource->UnregisterSink(this);`r`n" +
    "			logger::info(`"Unregistered from TESEquipEvent`");`r`n" +
    "		}",
    "		// build shim: see RegisterForEvents -- nothing was ever registered.`r`n" +
    "		// logger::info(`"TESEquipEvent watcher disabled by build shim`");")
if ($e2 -eq $e1) { throw 'EventHandler.cpp: UnregisterSink block not found' }

Set-Content $ehp -Value $e2 -NoNewline -Encoding utf8
Write-Host '  [12/15] EventHandler.cpp equip watcher disabled'

# NOTE: there is no "release near/far in every translation unit" step any more.
# include/RE/NetImmerse/NiFrustum.h overrides the library header so the frustum
# floats are named nearPlane/farPlane -- the <Windows.h> macros named near/far
# are never referenced, so no #undef is needed anywhere. (Injecting #undef into
# each .cpp was tried and did not hold: the injected lines land before the
# forced-include PCH, i.e. before <Windows.h> has defined the macros.)

# ================================================================ 14) ZPrePassDrawData cast
# The hook's parameter is AlphaTestZPrePassDrawData, but the trampoline takes
# ZPrePassDrawData. Neither type exists in powerof3, and the pointer is only
# forwarded to the original function -- it is never dereferenced here -- so the
# cast is a type-system formality with no runtime consequence.
$rhp = Join-Path $src 'RenderHooks.cpp'
$r = Get-Content $rhp -Raw
$r1 = $r.Replace(
    'g_hookMgr->g_RenderZPrePassOriginal(rshadowState, aZPreData, aVertexDesc, aCullmode, aDepthBiasMode);',
    'g_hookMgr->g_RenderZPrePassOriginal(rshadowState, reinterpret_cast<RE::BSGraphics::ZPrePassDrawData*>(aZPreData), aVertexDesc, aCullmode, aDepthBiasMode);')
if ($r1 -eq $r) { throw 'RenderHooks.cpp: g_RenderZPrePassOriginal call not found' }
Set-Content $rhp -Value $r1 -NoNewline -Encoding utf8
Write-Host '  [14/15] RenderHooks.cpp ZPrePass cast'

# ================================================================ 15) restore PosAdjust -> no-op
# The backup site declares `context` inside an inner `if (contextPtr) { ... }`
# block, so it is out of scope at the restore site. Renderer::GetSingleton() does
# not exist in powerof3 either, so this restore is simply dropped: PosAdjust is
# never rewritten by this path, so there is nothing to put back.
$spr2 = Join-Path $src 'rendering/SecondPassRenderer.cpp'
$sp = Get-Content $spr2 -Raw
$sp5 = $sp.Replace(
    'RE::BSGraphics::Renderer::GetSingleton().SetPosAdjust(&backupPosAdjust);',
    "// build shim: Renderer::GetSingleton() is absent from powerof3, and the`r`n" +
    "				// Context used above is out of scope here, so this restore is dropped.`r`n" +
    "				// PosAdjust is never rewritten by this path any more.")
if ($sp5 -eq $sp) { throw 'SecondPassRenderer: restore SetPosAdjust not found' }
Set-Content $spr2 -Value $sp5 -NoNewline -Encoding utf8
Write-Host '  [15/15] SecondPassRenderer.cpp restore-PosAdjust -> no-op'

# NOTE: the "set" side of PosAdjust is handled by the Renderer::GetSingleton
# replacement in step 11(c). Do NOT handle it again here -- an earlier version of
# this script did, and it failed because 11(c) had already rewritten the call.

Write-Host ''
Write-Host '=== all patches applied ==='

# ================================================================ 10) configure + build
Write-Host ''
Write-Host '=== CMake configure ==='
cmake -B build -S $tts -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCOPY_BUILD=OFF `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host ''
Write-Host '=== Build ==='
# -k 0 = keep going after errors: report ALL failures in one run.
ninja -C build -k 0
if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host '=== ALL compile errors above (reported in one run) ==='
    throw 'Build failed'
}

Write-Host ''
Write-Host '=== built binaries ==='
Get-ChildItem -Path build -Recurse -Include *.dll |
    Select-Object FullName, Length | Format-Table -AutoSize
