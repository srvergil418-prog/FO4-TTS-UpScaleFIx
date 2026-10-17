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

# ================================================================ 0) overlay CommonLibF4 include tree with dev-1.10.163
#
# THE CENTRAL PROBLEM (found at plugin LOAD time, after the DLL finally built):
#
#   The workflow checks out powerof3/CommonLibF4@master. master targets the
#   LATEST game version, so much of it is simply wrong for 1.10.163. Measuring
#   every REL::ID against the 1.10.163 Address Library:
#
#       master              11154 IDs,  9015 valid, 2139 MISSING   (80.8%)
#       dev-1.10.163        12575 IDs, 12575 valid,    0 missing   (100.0%)
#
#   A missing ID is fatal at LOAD time, not compile time. CommonLibF4 resolves
#   REL::ID lazily, and /inline static/ members plus function-local /static/
#   initialisers fire as soon as the enclosing class or function is ODR-used,
#   so the DLL shows a MessageBox before any plugin code runs:
#
#       CommonLibF4/src/REL/IDDB.cpp(127): id 2713225 not found!
#       game version: 1.10.163.0
#
#   That one was:
#       master  RE/Bethesda/ImageSpaceEffect.h:159  bDisable{ REL::ID(2713225) }
#       dev     RE/Bethesda/ImageSpaceEffect.h:192  bDisable{ REL::ID(1500590) }
#
#   The number is hard-coded in the HEADER, not read from an ID table. That is
#   why swapping VTABLE_IDs.h / RTTI_IDs.h alone was NOT enough: it fixes
#   references that come from those tables, but does nothing for a literal
#   constant inside a class body. The fix must bring in the real headers.
#
# WHY NOT SIMPLY CHECK OUT dev-1.10.163:
#   It has 337 files vs master 454 and does NOT define BSTriShape or
#   BSGeometry, which TTS needs.
#
# THE FIX -- overlay instead of switch:
#   /git checkout <ref> -- <path>/ updates only the files present in <ref> and
#   does NOT prune files that exist solely in the working tree. Overlaying dev:
#
#     in both        -> dev version   (correct 1.10.163 IDs)
#     only in master -> kept as-is    (BSGeometry.h, BSTriShape.h, ...)
#     only in dev    -> added
#
#   The path must be the WHOLE CommonLibF4 tree, not just CommonLibF4/include.
#   Overlaying include alone produced a header/source mismatch: master-only
#   REL/Segment.h survived next to dev REL/Relocation.h (which now defines
#   REL::Segment itself), and master .cpp files (REL/Module.cpp, REL/IDDB.cpp,
#   F4SE/API.cpp, RE/Bethesda/Calendar.cpp, ...) were compiled against dev
#   headers -- giving C2011 redefinitions and hundreds of follow-on errors.
#   Overlaying CommonLibF4 also brings dev CMakeLists.txt, so the source list
#   follows dev and master-only .cpp files are simply not compiled. Master-only
#   HEADERS stay on disk, which is what TTS needs (BSGeometry.h in particular).
#
#   VTABLE_IDs.h and RTTI_IDs.h get replaced as a side effect, so the earlier
#   hand-rolled table swap is no longer needed.
#
#   Two stale IDs do remain in master-only headers (RE/Bethesda/Sky.h has three
#   function-local statics). TTS only includes that header and never calls into
#   it, so those initialisers never run and the stale IDs are inert. Verified by
#   grepping the whole TTS tree for Sky::.
$clf4    = $commonlibf4
$clf4Inc = Join-Path $clf4 "CommonLibF4/include"

if ((Test-Path (Join-Path $clf4 ".git")) -and (Test-Path $clf4Inc)) {
    $iseHdr = Join-Path $clf4Inc "RE/Bethesda/ImageSpaceEffect.h"

    # Read the IDs with [regex]::Matches instead of Select-String.
    # Reason: when Select-String finds nothing, (..).Matches is $null, and
    # PowerShell runs a ForEach-Object body once with $_ = $null, so
    # $_.Groups[1] throws "Cannot index into a null array". Reading the text
    # ourselves and guarding every step keeps this from being fatal.
    function Get-RelIds([string]$path) {
        if (-not (Test-Path $path)) { return @() }
        $txt = [System.IO.File]::ReadAllText($path)
        $ms = [regex]::Matches($txt, 'REL::ID\((\d+)\)')
        $acc = @()
        foreach ($m in $ms) { $acc += $m.Groups[1].Value }
        if ($acc.Count -eq 0) { return @() }
        return ($acc | Select-Object -Unique)
    }

    $idsBefore = Get-RelIds $iseHdr
    Write-Host ("  ImageSpaceEffect.h IDs before: " + $(if ($idsBefore.Count) { $idsBefore -join ", " } else { "(none)" }))

    Push-Location $clf4
    try {
        & git fetch --depth=1 origin master 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "git fetch master failed" }
        & git checkout --force FETCH_HEAD -- CommonLibF4 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "git checkout FETCH_HEAD -- CommonLibF4 failed" }
        $head = (& git rev-parse --short FETCH_HEAD 2>$null)
        Write-Host ("  overlaid powerof3/master include tree (FETCH_HEAD=" + $head + ")")
    } finally {
        Pop-Location
    }

    # ------------------------------------------------------------------
    # Targeted REL::ID repair for master.
    #
    # powerof3/master carries IDs from a NEWER runtime; against the 1.10.163
    # address library only some of them resolve. Almost all of the stale ones
    # sit in inline static / function-local statics that are never ODR-used, so
    # they never initialise and are harmless. The ones in ImageSpaceEffect.h
    # ARE reachable from TTS, and they were the cause of the plugin-load
    # MessageBox "IDDB.cpp(127): id 2713225 not found!".
    #
    # Replacement values come from powerof3/dev-1.10.163, whose ID table is
    # 100%% valid for 1.10.163. Only these four differ; 1065542 and 775377 are
    # already correct on master.
    # ------------------------------------------------------------------
    # ------------------------------------------------------------------
    # master carries IDs from a NEWER runtime; many of them do not exist in
    # the 1.10.163 address library. Replacement values come from
    # powerof3/dev-1.10.163, whose ID table is 100% valid for 1.10.163.
    # Pairing rule: within the same header, match the whole line with
    # REL::ID(N) blanked out to REL::ID(#), then take the dev value.
    #
    # ONLY ids that are actually ODR-used at runtime are listed here. Most of
    # master's stale ids sit in inline static / function-local statics that
    # are never referenced, so they never initialise and never complain.
    # Rewriting those blindly would risk mis-pairing, and a wrong address
    # fails at RUNTIME (crash) instead of compile time -- much harder to
    # diagnose. So we add ids one reported failure at a time.
    # ------------------------------------------------------------------
    # ------------------------------------------------------------------
    # REL::ID REPAIR for powerof3/master against Fallout 4 1.10.163.
    #
    # master is built against a NEWER runtime, so 2448 of its 12797 REL::ID
    # values do not exist in the 1.10.163 address library. Those throw at
    # plugin load ("IDDB.cpp(127): id NNN not found!") or, worse, resolve to a
    # null address and crash the moment the pointer is called.
    #
    # Replacement values come from powerof3/dev-1.10.163 (100% valid for
    # 1.10.163), paired within the same header by matching the whole line with
    # REL::ID(N) blanked out to REL::ID(#).
    #
    # CRITICAL: only ids that are actually MISSING from 1.10.163 are rewritten.
    # An earlier revision replaced all 1626 paired ids, including 1265 that were
    # already correct -- that injected 1265 wrong addresses and made the plugin
    # crash during static initialisation (fault offset 0x0, i.e. a call through
    # a null function pointer). Every entry below was verified against the real
    # address library (Data/F4SE/Plugins/version-1-10-163-0.bin).
    #
    # Still-uncovered ids are left alone on purpose: most live in
    # RE/VTABLE_IDs.h, which is only resolved when VTABLE::X is referenced.
    # ------------------------------------------------------------------
    # Replacement table: masterID -> 1.10.163 ID.
    #
    # Kept in ci/id-fixes.json rather than inlined here: it runs to ~12000 entries
    # once the RTTI and VTABLE tables are covered too, which is far too large to
    # live in this script. Regenerate with: node tools/fix-ids-v2.mjs
    $idFixJson = Join-Path $PSScriptRoot "id-fixes.json"
    if (-not (Test-Path $idFixJson)) { throw "ci/id-fixes.json is missing -- run tools/fix-ids-v2.mjs to regenerate it" }
    $idFixes = @{}
    $idFixObj = [System.IO.File]::ReadAllText($idFixJson) | ConvertFrom-Json
    foreach ($prop in $idFixObj.PSObject.Properties) { $idFixes[$prop.Name] = [string]$prop.Value }
    Write-Host ("  id fix table: " + $idFixes.Count + " entries (master -> 1.10.163)")

    # Files whose ids may need rewriting. Each is visited independently so a
    # missing file (e.g. a master-only header that dev consolidated away) is
    # simply skipped rather than fatal.
    # FULL replacement: walk every header under include/ and rewrite REL::ID textually.
    # Why the whole tree instead of a file list: the crash happens during static
    # initialisation and produces no "not found" line, so there is no way to tell
    # Scan BOTH include/ and src/.
    #
    # Originally this only walked include/, so every REL::ID inside the library's
    # own .cpp files was left untouched and never entered the fix table. Those
    # IDs still get compiled into the DLL and resolved at run time, which is why
    # dead IDs kept surfacing one at a time as the user clicked through features
    # (e.g. "id 2269806 not found!" when opening the Debug tab -- that one is
    # RE/NetImmerse/NiMatrix3.cpp's NiMatrix3::ToEulerAnglesXYZ).
    # Measured on the current master overlay: src/ holds 34 REL::IDs, 19 of which
    # do not exist in the 1.10.163 address library.
    $idFixRoots = @(
        (Join-Path $clf4 "CommonLibF4/include"),
        (Join-Path $clf4 "CommonLibF4/src")
    )
    $totalFixed = 0
    $filesTouched = 0
    $allHeaders = @()
    foreach ($root in $idFixRoots) {
        if (Test-Path $root) {
            $allHeaders += Get-ChildItem -Path $root -Recurse -Include *.h,*.hpp,*.inl,*.cpp -File -ErrorAction SilentlyContinue
        }
    }
    Write-Host ("  id fix scope: " + $allHeaders.Count + " files under include/ + src/")
    foreach ($f in $allHeaders) {
        $before = [System.IO.File]::ReadAllText($f.FullName)
        $after = $before
        $hits = 0

        # Only touch IDs that actually appear in this file. Iterating the whole
        # table per file would be ~3.6 million String.Contains calls.
        $seen = New-Object System.Collections.Generic.HashSet[string]
        foreach ($m in [regex]::Matches($before, "REL::ID\s*\(\s*(\d+)\s*\)")) { [void]$seen.Add($m.Groups[1].Value) }
        foreach ($m in [regex]::Matches($before, "REL::ID\s+[A-Za-z_]\w*\s*[{=]\s*(\d+)")) { [void]$seen.Add($m.Groups[1].Value) }

        foreach ($id in $seen) {
            if (-not $idFixes.ContainsKey($id)) { continue }
            $new = $idFixes[$id]

            # Form 1: REL::ID(123)          -- plain replacement is exact and cheap.
            $paren = "REL::ID(" + $id + ")"
            if ($after.Contains($paren)) {
                $after = $after.Replace($paren, "REL::ID(" + $new + ")")
                $hits++
            }

            # Form 2: REL::ID Name{ 123 } / REL::ID Name = 123  -- used by
            # RTTI_IDs.h. Must be a regex so the bare number is only matched
            # right after that declaration (a plain .Replace on the digits would
            # corrupt unrelated numbers). \b stops 123 from matching 1234.
            $brace = "REL::ID(\s+[A-Za-z_]\w*\s*[{=]\s*)" + $id + "\b"
            $rxBrace = [regex]::new($brace)
            $braceHits = $rxBrace.Matches($after).Count
            if ($braceHits -gt 0) {
                $after = $rxBrace.Replace($after, ("REL::ID" + '${1}' + $new))
                $hits += $braceHits
            }
        }

        if ($after -ne $before) {
            [System.IO.File]::WriteAllText($f.FullName, $after)
            $totalFixed += $hits
            $filesTouched++
        }
    }
    Write-Host ("  id repair: " + $totalFixed + " stale REL::ID replaced with 1.10.163 values")

    # ================================================================
    # Disable REL::Relocation members whose ID is absent from the 1.10.163
    # address library and has no 1.10.163 counterpart to be mapped to.
    #
    # Why this is mandatory rather than cosmetic:
    #   inline static REL::Relocation<T> x{ REL::ID(N) };
    # has STATIC STORAGE DURATION, so its dynamic initialisation is run by the
    # CRT during DLL load -- whether or not TTS ever mentions x. If N is
    # unknown, IDDB calls stl::report_and_fail(), which pops a MessageBox and
    # terminates the process BEFORE DllMain. That is exactly the failure
    # observed: tts-trace.log stayed completely empty (not even
    # "DllMain PROCESS_ATTACH") while Fallout4.exe sat there with a dialog
    # whose title was "TrueThroughScope.dll" and whose text named the missing
    # id. So this is not laziness about dead code -- it is the blocker itself.
    #
    # Counter-example worth remembering: RTTI_IDs.h and VTABLE_IDs.h use
    #   inline constexpr REL::ID X{ N };
    # which is a compile-time constant and is NEVER resolved at load time.
    # Their leftovers are inert; only REL::Relocation matters here.
    #
    # The list comes from tools/gen-dead-ids.mjs, run offline because the
    # address library lives on the developer machine and not on the runner.
    #
    # If TTS genuinely needs one of these members, the build now FAILS at the
    # point of use -- which is the signal we want: that member then has to be
    # given a real 1.10.163 ID sourced from the author PDB, not guessed.
    $deadJson = Join-Path $PSScriptRoot "dead-ids.json"
    if (-not (Test-Path $deadJson)) { throw "ci/dead-ids.json is missing -- run tools/gen-dead-ids.mjs to regenerate it" }
    $deadIds = @{}
    $deadObj = [System.IO.File]::ReadAllText($deadJson) | ConvertFrom-Json
    foreach ($prop in $deadObj.PSObject.Properties) { $deadIds[$prop.Name] = $true }
    Write-Host ("  dead id list: " + $deadIds.Count + " ids absent from the 1.10.163 address library")

    $script:deadIds = $deadIds
    $script:deadDisabled = 0
    $deadEval = [System.Text.RegularExpressions.MatchEvaluator]{
        param($m)
        $id = $m.Groups[2].Value
        if ($script:deadIds.ContainsKey($id)) {
            $script:deadDisabled++
            return ("// [TTS CI] disabled below: ID " + $id + " is absent from the 1.10.163 address library and has no counterpart there.`r`n// ")
        }
        return $m.Value
    }
    $rxDead = [regex]::new('(?m)^[ \t]*((?:inline[ \t]+static[ \t]+|static[ \t]+inline[ \t]+)REL::Relocation\s*<[^\r\n]*?REL::ID\(\s*(\d+)\s*\)[^\r\n]*)')
    foreach ($f in $allHeaders) {
        $before = [System.IO.File]::ReadAllText($f.FullName)
        if ($before -notmatch "REL::Relocation") { continue }
        $after = $rxDead.Replace($before, $deadEval)
        if ($after -ne $before) { [System.IO.File]::WriteAllText($f.FullName, $after) }
    }
    # NOTE: the regex deliberately requires "inline". A class-scope
    #     inline static REL::Relocation<...> x{ REL::ID(N) };
    # is initialised by the CRT at DLL load, so an unknown N is fatal before
    # DllMain. A function-local
    #     static REL::Relocation<...> func{ REL::ID(N) };
    # only resolves when that line is first executed, so leaving it alone is
    # safe as long as TTS never calls that function -- and commenting it out
    # would turn a harmless entry into a hard compile error. Measured split on
    # the current master tree: 1 class-scope vs 129 function-local.
    Write-Host ("  disabled " + $script:deadDisabled + " class-scope inline static REL::Relocation members whose IDs do not exist in 1.10.163") -ForegroundColor Yellow
    if (-not (Test-Path $iseHdr)) { throw "ImageSpaceEffect.h disappeared after the overlay" }

    $idsAfter = Get-RelIds $iseHdr
    Write-Host ("  ImageSpaceEffect.h IDs after : " + $(if ($idsAfter.Count) { $idsAfter -join ", " } else { "(none)" }))

    if ($idsAfter -contains "2713225") {
        throw "ImageSpaceEffect.h still carries the stale master ID 2713225 -- the repair did not apply"
    }
    if (-not ($idsAfter -contains "1500590")) {
        throw "ImageSpaceEffect.h is missing the 1.10.163 ID 1500590 -- unexpected content after overlay"
    }
    # Types that exist only on master must survive the overlay.
    foreach ($must in @("RE/Bethesda/BSGeometry.h")) {
        if (-not (Test-Path (Join-Path $clf4Inc $must))) {
            throw ("overlay pruned a master-only header we need: " + $must)
        }
    }
    Write-Host "  include overlay verified (dev IDs in, master-only types kept)" -ForegroundColor Green
} else {
    Write-Host "  (not a git checkout or include dir missing -- skipping include overlay)" -ForegroundColor Yellow
}
Write-Host ""
# ================================================================ 1) hkVector4.h
$hv = Join-Path $commonlibf4 'CommonLibF4/include/RE/Havok/hkVector4.h'
$t = Get-Content $hv -Raw
$t2 = $t.Replace('hkVector4f& GetNormalized()', 'hkVector4f GetNormalized()')
if ($t2 -eq $t) { Write-Host '  [1/9] hkVector4.h: pattern not found -- skipping' -ForegroundColor Yellow }
Set-Content $hv -Value $t2 -NoNewline -Encoding utf8
Write-Host '  [1/9] hkVector4.h'

# ================================================================ 2) API.cpp
$api = Join-Path $commonlibf4 'CommonLibF4/src/F4SE/API.cpp'
$a = Get-Content $api -Raw
$a2 = $a.Replace(
    'log::info("{} v{}", GetPluginName(), GetPluginVersion());',
    'log::info("{} v{}", GetPluginName(), GetPluginVersion().string());')
if ($a2 -eq $a) { Write-Host '  [2/9] API.cpp: pattern not found -- skipping' -ForegroundColor Yellow }
Set-Content $api -Value $a2 -NoNewline -Encoding utf8
Write-Host '  [2/9] API.cpp'

# ================================================================ 2b) F4SE/Interfaces.h -- the 0.6.23 loader ABI
#
# THE PLUGIN-LOAD CRASH (0xC0000005, fault offset 0x0).
#
# F4SEInterface in F4SE 0.6.23 -- the loader shipped with game version 1.10.163,
# which is what this build targets -- ENDS at GetPluginInfo:
#
#   +0   f4seVersion        UInt32
#   +4   runtimeVersion     UInt32
#   +8   editorVersion      UInt32
#   +12  isEditor           UInt32
#   +16  QueryInterface     fn ptr
#   +24  GetPluginHandle    fn ptr
#   +32  GetReleaseIndex    fn ptr
#   +40  GetPluginInfo      fn ptr   <- LAST member; the struct is 48 bytes
#
# powerof3/master declares one more member at +48:
#
#   +48  GetSaveFolderName  fn ptr   <- F4SE 0.7.1+ ONLY, not in 0.6.23
#
# (the upstream header even labels it "// 0.7.1+", and the same file marks
#  GetPluginInfo "// 0.6.22+", so the version dependency is upstream knowledge).
#
# CommonLibF4/src/F4SE/API.cpp::Init() then does, unconditionally:
#
#   storage.saveFolderName = intfc.GetSaveFolderName();
#
# With the 0.6.23 loader that slot holds whatever follows the interface struct
# in the loader image -- not a code pointer. Calling it derails into unmapped
# memory. That explains EVERY piece of the observed evidence at once:
#
#   - exception code 0xC0000005 (access violation) raised inside F4SE::Init
#   - fault offset 0x0000000000000000, "faulting module: unknown" (target
#     address 0 belongs to no loaded module -> a call through a null pointer)
#   - xSE PluginPreloader logs "loaded" then "unloaded" ~40ms later
#   - f4se.log: "disabled, fatal error occurred while loading plugin"
#   - NO MessageBox: this is not stl::report_and_fail(), which would pop one
#   - the load trace reaches "after PreInit" and then dies before
#     "after F4SE::Init" -- Init is the only frame in between
#
# Why the earlier REL::ID work never helped: that is a redirect-table problem
# and this is a struct-layout problem. Overlaying master brought in a newer
# interface declaration along with newer IDs.
#
# Fix: comment the member out and make the accessor return an empty view.
# Scope check: the TTS tree never calls GetSaveFolderName (grepped), so the
# only consumer is the CommonLibF4 accessor itself. Every OTHER interface in
# this header was diffed field-by-field against the 0.6.23 SDK
# (f4se_0_06_23/src/f4se/f4se/PluginAPI.h) and matches exactly -- Messaging,
# Scaleform, Papyrus, Serialization, Task, Object, Trampoline and PluginInfo
# all agree, so this is the single point of incompatibility.
#
# Implementation notes: substring replacement via String.Replace, deliberately
# NOT -replace (that is a regex, and these needles contain ( ) * . ) and NOT
# whole lines, so the patch survives CRLF/LF changes and tab-indent drift in
# the upstream checkout. If a needle ever stops matching, the build FAILS
# rather than silently shipping a DLL that crashes on load.

$intf = Join-Path $commonlibf4 "CommonLibF4/include/F4SE/Interfaces.h"
if (-not (Test-Path $intf)) { throw "F4SE/Interfaces.h not found: $intf" }
$intfText = [System.IO.File]::ReadAllText($intf)
$intfOrig = $intfText
$intfText = $intfText.Replace(
    'const char*(F4SEAPI* GetSaveFolderName)(void);',
    '/* [TTS CI] GetSaveFolderName is F4SE 0.7.1+; the 1.10.163 loader (F4SE 0.6.23) has no such entry */')
$intfText = $intfText.Replace(
    'return GetProxy().GetSaveFolderName();',
    'return {};')
if ($intfText -eq $intfOrig) { throw "F4SE/Interfaces.h: the 0.6.23 ABI patch matched nothing (upstream layout changed?)" }
if ($intfText -match 'GetProxy\(\)\.GetSaveFolderName') { throw "F4SE/Interfaces.h: the GetSaveFolderName proxy call survived the patch" }
if ($intfText -match 'F4SEAPI\* GetSaveFolderName') { throw "F4SE/Interfaces.h: the F4SEInterface member survived the patch" }
[System.IO.File]::WriteAllText($intf, $intfText, (New-Object System.Text.UTF8Encoding($false)))
Write-Host '  [2b/9] F4SE/Interfaces.h -- 0.6.23 ABI (GetSaveFolderName removed)' -ForegroundColor Green

# ================================================================ 3) REX/LOG.cpp
#
# The wide-char LOG overload must be converted to UTF-8 before reaching spdlog.
# This was needed against master; whether dev-1.10.163 still has the overload is
# not guaranteed, so every step here is BEST-EFFORT: if the pattern is absent we
# warn and move on instead of aborting the build.
$lg = Join-Path $commonlibf4 "CommonLibF4/src/REX/REX/LOG.cpp"
$g = Get-Content $lg -Raw
if ($g -notmatch "REX/REX/CONVERT\.h") {
    $g = $g.Replace("#include `"REX/W32/USER32.h`"",
                    "#include `"REX/W32/USER32.h`"`r`n#include `"REX/REX/CONVERT.h`"")
}
$rx = [regex]"(?s)void LOG\(const std::source_location a_loc, const LOG_LEVEL a_level, const std::wstring_view a_fmt\)\s*\{.*?\r?\n\t\}"
$m = $rx.Match($g)
if (-not $m.Success) {
    Write-Host "  [3/9] REX/LOG.cpp: wide overload not found -- skipping (dev may not need it)" -ForegroundColor Yellow
} else {
    $repl = "void LOG(const std::source_location a_loc, const LOG_LEVEL a_level, const std::wstring_view a_fmt)`r`n`t{`r`n`t`t// spdlog accepts narrow chars only; convert UTF-16 -> UTF-8 first`r`n`t`tstd::string narrow;`r`n`t`tif (!REX::UTF16_TO_UTF8(a_fmt, narrow)) {`r`n`t`t`tnarrow = `"<encoding error>`";`r`n`t`t}`r`n`t`tLOG(a_loc, a_level, std::string_view{ narrow });`r`n`t}"
    $g2 = $g.Remove($m.Index, $m.Length).Insert($m.Index, $repl)
    if ($g2 -eq $g) {
        Write-Host "  [3/9] REX/LOG.cpp: replacement had no effect -- skipping" -ForegroundColor Yellow
    } else {
        Set-Content $lg -Value $g2 -NoNewline -Encoding utf8
        Write-Host "  [3/9] REX/LOG.cpp"
    }
}

# ================================================================ 4) REL/Module.h
#
# The author private library exposes IsF4/IsVR; powerof3 master did not. Add them
# if absent. Best-effort: warn and continue if the class anchor cannot be found.
#
# NOTE: the consolidated-header cleanup above deletes REL/Module.h on purpose --
# dev-1.10.163 folded REL::Module into REL/Relocation.h. So on a dev overlay this
# file is EXPECTED to be absent and this whole step is a no-op. It still matters
# if the overlay is ever skipped or the upstream layout changes.
$mod = Join-Path $commonlibf4 "CommonLibF4/include/REL/Module.h"
if (-not (Test-Path $mod)) {
    Write-Host "  [4/9] REL/Module.h absent (removed by the dev overlay) -- skipping" -ForegroundColor Yellow
} else {
    $mo = Get-Content $mod -Raw
    if ($mo -match "static bool IsF4") {
        Write-Host "  [4/9] REL/Module.h already patched"
    } else {
        $mo2 = $mo -replace "(class\s+Module\s*\{)", "`$1`r`n`tpublic:`r`n`t`t// Added by build shim: this build targets the desktop (non-VR) runtime.`r`n`t`t[[nodiscard]] static bool IsF4() { return true; }`r`n`t`t[[nodiscard]] static bool IsVR() { return false; }"
        if ($mo2 -eq $mo) {
            Write-Host "  [4/9] REL/Module.h: could not insert IsF4/IsVR -- skipping" -ForegroundColor Yellow
        } else {
            Set-Content $mod -Value $mo2 -NoNewline -Encoding utf8
            Write-Host "  [4/9] REL/Module.h (IsF4/IsVR)"
        }
    }
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
# (b) NOTE: stream->topObjects[0] needs NO rewrite any more. The build shim now
#     provides BSStream as a self-contained class whose topObjects is a plain
#     std::vector, so the original subscript expression compiles as-is.
$nif = Join-Path $src 'NiFLoader.cpp'
$nf = Get-Content $nif -Raw
$nf2 = $nf.Replace(
    'NiShadeProperty* shadeProperty = triShape->QShaderProperty();',
    'NiShadeProperty* shadeProperty = nullptr;  // build shim: QShaderProperty() unavailable in powerof3')
if ($nf2 -eq $nf) { throw 'NiFLoader.cpp: QShaderProperty pattern not found' }
Set-Content $nif -Value $nf2 -NoNewline -Encoding utf8
Write-Host '  [8/9] NiFLoader.cpp (QShaderProperty)'

# SettingsPanel.cpp: imgui 1.92 removed the ImGuiKey_Mod* spellings.
$set = Join-Path $src 'UI/Panels/SettingsPanel.cpp'
$s = Get-Content $set -Raw
$s1 = $s.Replace('ImGuiKey_ModCtrl', 'ImGuiMod_Ctrl')
$s2 = $s1.Replace('ImGuiKey_ModShift', 'ImGuiMod_Shift')
$s3 = $s2.Replace('ImGuiKey_ModAlt', 'ImGuiMod_Alt')
if ($s3 -eq $s) { throw 'SettingsPanel.cpp: ImGuiKey_Mod* pattern not found' }
Set-Content $set -Value $s3 -NoNewline -Encoding utf8
Write-Host '  [8/9] SettingsPanel.cpp (ImGuiMod_*)'

# ================================================================ 8c) CMakeLists.txt HLSL copy paths
# TTS's CMakeLists.txt hardcodes THREE absolute output paths that only exist on
# the author's machine:
#     G:/Games/FO4_SO/mods/TTS True Through Scope/Shaders/XiFeiLi
#     G:/Games/Fallout 4 ori/Fallout 4/Mo2/mods/TTS/Shaders/XiFeiLi
#     I:/Games/Fallout 4 Pack/FO4SO_1.5/mods/TTS/Shaders/XiFeiLi
#
# set_hlsl_properties() compiles each .cso into path #1, then emits an UNGUARDED
# POST_BUILD add_custom_command (inside `if(PATH_COUNT GREATER 1)`) that
# mkdir+copy's it into paths #2 and #3. On CI those drives do not exist, so the
# DLL links fine and then ninja dies with:
#     Error creating directory "G:/Games/...": No such file or directory
#
# Fix: collapse the list to the single path the compiler actually writes to.
# PATH_COUNT then equals 1, the `GREATER 1` branch never runs, and no copy
# command is generated at all -- no need to touch the macro body.
$cml = Join-Path $tts 'CMakeLists.txt'
$cRaw = Get-Content $cml -Raw
# Normalise line endings to LF first: the CI checkout is CRLF while local is LF.
$c = $cRaw -replace "`r`n", "`n"
$c0 = $c

$c = $c.Replace(
    'option(COPY_BUILD "Copy the build output to the Fallout 4 directory." ON)',
    'option(COPY_BUILD "Copy the build output to the Fallout 4 directory." OFF)  # build shim: author-local paths, default OFF')

# Remove the two author-local entries line by line instead of matching the whole
# multi-line block. The original block mixes 4-space and TAB indentation, and the
# TAB does not survive every checkout/editor round trip, which made the block
# match fail silently. Matching per line on the quoted path prefix is immune to
# whatever whitespace precedes it.
$before = $c
$c = $c -replace '(?m)^[ \t]*"G:/Games/Fallout 4 ori/[^"]*"[ \t]*\n', ''
$c = $c -replace '(?m)^[ \t]*"I:/Games/Fallout 4 Pack/[^"]*"[ \t]*\n', ''
if ($c -eq $before) {
    throw 'CMakeLists.txt: no author-local HLSL output path line was removed'
}

# Replace the (now single-entry) first path with a build-local directory, so the
# shader compile output still lands somewhere writable on the runner.
# NOTE: the replacement must be a double-quoted string -- inside single quotes
# PowerShell treats `n as two literal characters rather than a newline.
$hlslReplacement = '    "${CMAKE_CURRENT_BINARY_DIR}/hlsl"' + "`n"
$c = $c -replace '(?m)^[ \t]*"G:/Games/FO4_SO/[^"]*"[ \t]*\n', $hlslReplacement

if ($c -match 'Fallout 4 ori' -or $c -match 'FO4SO_1\.5' -or $c -match 'G:/Games' -or $c -match 'I:/Games') {
    throw 'CMakeLists.txt: an author-local path survived the patch'
}
if ($c -notmatch 'CMAKE_CURRENT_BINARY_DIR}/hlsl') {
    throw 'CMakeLists.txt: replacement HLSL output path is missing'
}
Set-Content $cml -Value $c -NoNewline -Encoding utf8
Write-Host '  [8c/9] CMakeLists.txt HLSL output paths -> single build-local dir'

# NOTE: there is no NiFrustum member-rename step any more.
# include/RE/NetImmerse/NiFrustum.h overrides the library header with members
# named nearPlane/farPlane -- exactly what TTS's sources already write. This
# avoids <Windows.h>'s empty near/far macros entirely, with no #undef games.


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
    "`t`t`t`tRE::BSGraphics::Renderer::GetSingleton().SetPosAdjust(&scopePosAdjust);",
    "`t`t`t`t// PosAdjust MUST actually be written. Skipping it (the old suspect test)`r`n" +
    "`t`t`t`t// left the scope camera position unsynced, so the sky and the scope picture`r`n" +
    "`t`t`t`t// were projected with a stale matrix -- user saw the TTS image floating above`r`n" +
    "`t`t`t`t// their head with the frame gone. Offset chain: 0x2240 (shadowState)`r`n" +
    "`t`t`t`t// + 0x210 (PosAdjust inside CameraStateData).`r`n" +
    "`t`t`t`tcontext->shadowState.PosAdjust = scopePosAdjust;")
if ($sp4 -eq $sp3) { throw 'SecondPassRenderer: Renderer::GetSingleton block not found' }

Set-Content $spr -Value $sp4 -NoNewline -Encoding utf8
Write-Host '  [11/12] SecondPassRenderer.cpp downgraded (DrawWorld, SetCameraData, PosAdjust)'

    # ================================================================ 11b) NiMatrix3::ToEulerAnglesXYZ
    #
    # src/RE/NetImmerse/NiMatrix3.cpp reaches the engine through
    # REL::ID(2269806..2269825). That entire ID family belongs to the newer address
    # library and NONE of it exists in the 1.10.163 database; dev-1.10.163 does not
    # even ship this .cpp, so the replacement table cannot help. The only value the
    # dev branch does give is ToEulerAnglesXYZ == REL::ID(34114), which confirms the
    # family sits in the small-ID range over there -- but nothing hands us the other
    # eleven, and ID->offset ordering gives no adjacency to exploit.
    #
    # So all twelve get replaced at once: they are pure math (rotation matrix <->
    # Euler angles), and To/From are written as exact inverses of each other so TTS
    # stays self-consistent when it stores a rotation and reads it back.
    # Observed symptoms before this: "id 2269806 not found!" on the Debug tab, and
    # "id 2269813 not found!" while a save was loading (ScopeCamera.cpp:211 calls
    # FromEulerAnglesXYZ on the scope camera).
    # Replacement text lives in ci/NiMatrix3.EulerAngles.inc.
    $nm3 = Join-Path $clf4 "CommonLibF4/src/RE/NetImmerse/NiMatrix3.cpp"
    $nm3inc = Join-Path $PSScriptRoot "NiMatrix3.EulerAngles.inc"
    if ((Test-Path $nm3) -and (Test-Path $nm3inc)) {
        $nm3text = [System.IO.File]::ReadAllText($nm3)
        if (($nm3text.Contains("REL::ID(2269806)")) -and (-not $nm3text.Contains("[TTS CI] These 12 functions"))) {
            $implText = [System.IO.File]::ReadAllText($nm3inc).TrimEnd()
            # Replace everything from the first engine-backed overload to (not including)
            # the closing brace of the enclosing namespace.
            $rxNm3 = [regex]::new("(?s)bool\s+NiMatrix3::ToEulerAnglesXYZ\(float&.*(?=\r?\n\}\s*\z)")
            if (-not $rxNm3.IsMatch($nm3text)) { throw "NiMatrix3: Euler-angle block regex did not match" }
            $nm3new = $rxNm3.Replace($nm3text, $implText, 1)
            if ($nm3new -eq $nm3text) { throw "NiMatrix3: Euler-angle block patch did not match" }
            [System.IO.File]::WriteAllText($nm3, $nm3new, (New-Object System.Text.UTF8Encoding($false)))
            Write-Host "  [11b] NiMatrix3.cpp Euler-angle family (12 fns) -> pure-math implementation" -ForegroundColor Green
        } else {
            Write-Host "  [11b] NiMatrix3.cpp already patched or ID absent -- skipping" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  [11b] NiMatrix3.cpp or the .inc template missing -- skipping" -ForegroundColor Yellow
    }

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

# ================================================================ 11) expose the PDB to the artifact
#
# Why this exists: a crash log gives us only "TrueThroughScope.dll+0014169", and
# an RVA is meaningless without our own symbol table -- the author's PDB describes
# the author's binary, not ours. Matching against it is how you end up staring at
# std::regex internals wondering what it has to do with scopes.
#
# The workflow's artifact glob is "build/**/TrueThroughScope.dll" and editing that
# workflow needs the PAT 'workflow' scope, which we do not have (HTTP 403). So do
# not touch the workflow: park a copy of the PDB in a subdirectory under the same
# file name the glob already matches. upload-artifact preserves relative paths, so
# the downloader can tell the two apart and rename this one back to .pdb.
$pdbFile = Get-ChildItem -Path build -Recurse -Filter 'TrueThroughScope.pdb' -File -ErrorAction SilentlyContinue |
           Select-Object -First 1
if ($pdbFile) {
    $slot = Join-Path (Split-Path $pdbFile.FullName -Parent) 'symbols-as-dll'
    New-Item -ItemType Directory -Force -Path $slot | Out-Null
    Copy-Item -Path $pdbFile.FullName -Destination (Join-Path $slot 'TrueThroughScope.dll') -Force
    Write-Host ("  [artifact] PDB exposed as " + (Join-Path $slot 'TrueThroughScope.dll'))
} else {
    Write-Host '  [artifact] WARNING: no TrueThroughScope.pdb was produced -- crash RVAs stay untranslatable'
    Get-ChildItem -Path build -Recurse -Include *.pdb,*.ilk -ErrorAction SilentlyContinue |
        Select-Object FullName | Format-Table -AutoSize
}
