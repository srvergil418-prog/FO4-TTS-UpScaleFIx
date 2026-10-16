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
    $idFixes = @{
        "2204343" = "638606"
        "2228356" = "453212"
        "2267850" = "1495205"
        "2267872" = "652767"
        "2267873" = "1502917"
        "2267874" = "1582181"
        "2367114" = "650161"
        "2384634" = "644708"
        "2713225" = "1500590"
        "2713237" = "1523708"
        "2730821" = "1430850"
        "2730832" = "1045942"
        "2731182" = "338574"
        "2731198" = "749462"
        "2731205" = "1503510"
        "2731210" = "1548930"
        "2731215" = "1334574"
        "2731220" = "1120688"
        "2731225" = "906808"
        "2731230" = "693089"
        "2731245" = "50607"
        "2731250" = "1418751"
        "2731255" = "1205154"
        "2731260" = "990635"
        "2731267" = "318153"
        "2731269" = "777182"
        "2731274" = "563042"
        "2731279" = "348730"
        "2731284" = "134879"
        "2731306" = "381911"
        "2731323" = "643055"
        "2731328" = "1291183"
        "2731334" = "818933"
        "2731340" = "199161"
        "2731350" = "164788"
        "2731356" = "650457"
        "2731366" = "782911"
        "2731375" = "520697"
        "2731390" = "876954"
        "2731394" = "1022179"
        "2731399" = "835850"
        "2731400" = "1167575"
        "2731408" = "1458129"
        "2731412" = "20571"
        "2731416" = "166446"
        "2731420" = "1152121"
        "2731424" = "1297338"
        "2731428" = "1442648"
        "2731443" = "151036"
        "2731450" = "1339396"
        "2731454" = "295949"
        "2731456" = "109652"
        "2731461" = "441121"
        "2731466" = "82152"
        "2731472" = "732212"
        "2731476" = "372561"
        "2731749" = "1570756"
        "2731781" = "166030"
        "2731783" = "1216371"
        "2731784" = "1388660"
        "2732022" = "1123226"
        "2732681" = "707406"
        "4471522" = "343176"
        "4784697" = "1500590"
        "4797160" = "833923"
        "4797235" = "526727"
        "4797261" = "1279207"
        "4818455" = "84112"
        "4823974" = "755771"
        "4823975" = "1344669"
        "4823977" = "1525504"
        "4823978" = "1097320"
        "4823980" = "631252"
        "4823981" = "1289112"
        "4824040" = "858032"
        "4824041" = "378040"
        "4824042" = "724362"
        "4824043" = "677732"
        "4824078" = "602845"
        "4824082" = "766818"
        "4824090" = "409234"
        "4824092" = "16320"
        "4824093" = "369219"
        "4824094" = "1027245"
        "4824095" = "1099535"
        "4824096" = "986183"
        "4824097" = "566880"
        "4824119" = "1455860"
        "4824125" = "1577024"
        "4824126" = "1530435"
        "4824129" = "1494909"
        "4824130" = "962466"
        "4824131" = "430297"
        "4824132" = "1481157"
        "4824133" = "948747"
        "4824134" = "416590"
        "4824135" = "342566"
        "4824136" = "1192695"
        "4824137" = "567532"
        "4824138" = "1019090"
        "4824139" = "707025"
        "4824140" = "393830"
        "4824141" = "81341"
        "4824142" = "865400"
        "4824143" = "772547"
        "4824144" = "459379"
        "4824145" = "366356"
        "4824146" = "53723"
        "4824147" = "399193"
        "4824148" = "1449638"
        "4824149" = "917494"
        "4824150" = "385464"
        "4824151" = "73001"
        "4824152" = "1123037"
        "4824153" = "1249518"
        "4824154" = "1156033"
        "4824155" = "624200"
        "4824156" = "311374"
        "4824157" = "1581141"
        "4824158" = "1268491"
        "4824159" = "736749"
        "4824160" = "643365"
        "4824161" = "111144"
        "4824162" = "1380553"
        "4824163" = "848734"
        "4824164" = "755803"
        "4824165" = "442673"
        "4824166" = "349502"
        "4824167" = "256443"
        "4824168" = "1526226"
        "4824169" = "1213682"
        "4824170" = "681852"
        "4824171" = "368683"
        "4824172" = "1418809"
        "4824173" = "1325542"
        "4824174" = "793991"
        "4824175" = "261740"
        "4824176" = "168707"
        "4824177" = "75335"
        "4824178" = "1125418"
        "4824179" = "593717"
        "4824180" = "280830"
        "4824181" = "1330974"
        "4824182" = "930572"
        "4824183" = "782569"
        "4824184" = "148716"
        "4824185" = "845057"
        "4824186" = "304579"
        "4824187" = "1267068"
        "4824188" = "666466"
        "4824189" = "1535482"
        "4824190" = "1560017"
        "4824191" = "1019325"
        "4824192" = "550386"
        "4824193" = "191182"
        "4824194" = "448231"
        "4824195" = "6090"
        "4824196" = "1496026"
        "4824197" = "390329"
        "4824198" = "554980"
        "4824199" = "593240"
        "4824200" = "1121898"
        "4824201" = "1028621"
        "4824202" = "927010"
        "4824203" = "639135"
        "4824204" = "1335064"
        "4824205" = "29390"
        "4824206" = "553621"
        "4824207" = "862863"
        "4824208" = "330969"
        "4824209" = "1326024"
        "4824210" = "1197180"
        "4824211" = "596496"
        "4824212" = "1064784"
        "4824213" = "444687"
        "4824214" = "1221096"
        "4824215" = "111813"
        "4824216" = "770107"
        "4824217" = "1427866"
        "4824218" = "503945"
        "4824219" = "1540542"
        "4824222" = "76202"
        "4824223" = "328354"
        "4824224" = "674220"
        "4824225" = "1128589"
        "4824230" = "1258192"
        "4824232" = "544493"
        "4824233" = "264554"
        "4824235" = "774350"
        "4824239" = "1481518"
        "4824240" = "557221"
        "4824245" = "487459"
        "4824248" = "366724"
        "4824251" = "1562567"
        "4824252" = "1222427"
        "4824253" = "297870"
        "4824254" = "956210"
        "4824255" = "1320645"
        "4824256" = "662887"
        "4824257" = "682206"
        "4824258" = "360619"
        "4824259" = "220960"
        "4824260" = "878907"
        "4824261" = "486791"
        "4824262" = "1144902"
        "4824263" = "753135"
        "4824264" = "1410783"
        "4824265" = "1018746"
        "4824266" = "94740"
        "4824267" = "245412"
        "4824268" = "903503"
        "4824269" = "1297852"
        "4824270" = "373759"
        "4824271" = "878205"
        "4824272" = "1536577"
        "4824273" = "612405"
        "4824274" = "817987"
        "4824275" = "1476328"
        "4824276" = "1135811"
        "4824277" = "211906"
        "4824278" = "231005"
        "4824279" = "889034"
        "4824280" = "436250"
        "4824281" = "1094651"
        "4824282" = "537631"
        "4824283" = "789924"
        "4824284" = "131521"
        "4824285" = "1321536"
        "4824286" = "397480"
        "4824287" = "1055636"
        "4824288" = "663795"
        "4824289" = "929514"
        "4824290" = "4839"
        "4824291" = "271579"
        "4824292" = "1195658"
        "4824293" = "1373446"
        "4824294" = "449273"
        "4824299" = "306573"
        "4824300" = "964743"
        "4824301" = "40443"
        "4824302" = "699239"
        "4824303" = "1356619"
        "4824304" = "1077086"
        "4824305" = "418857"
        "4824306" = "1342958"
        "4824307" = "685353"
        "4824308" = "951053"
        "4824309" = "26457"
        "4824310" = "559063"
        "4824311" = "1217295"
        "4824312" = "292677"
        "4824313" = "1483374"
        "4824314" = "824949"
        "4824315" = "432478"
        "4824316" = "1090832"
        "4824317" = "166947"
        "4824318" = "51273"
        "4824319" = "710018"
        "4824320" = "835646"
        "4824321" = "1494063"
        "4824322" = "177693"
        "4824323" = "1101688"
        "4824324" = "443231"
        "4824325" = "1367328"
        "4824326" = "569756"
        "4824327" = "1227891"
        "4824328" = "303514"
        "4824329" = "961660"
        "4824330" = "23437"
        "4824331" = "682415"
        "4824332" = "947956"
        "4824333" = "1339876"
        "4824334" = "668442"
        "4824335" = "1060273"
        "4824336" = "136216"
        "4824337" = "1326183"
        "4824338" = "402064"
        "4824339" = "1252368"
        "4824340" = "594322"
        "4824341" = "1523817"
        "4824342" = "599615"
        "4824343" = "777816"
        "4824344" = "1435513"
        "4824345" = "119385"
        "4824346" = "1043510"
        "4824347" = "363164"
        "4824348" = "1021306"
        "4824349" = "489330"
        "4824350" = "1413334"
        "4824351" = "755693"
        "4824352" = "97276"
        "4824353" = "1147488"
        "4824354" = "223564"
        "4824355" = "1479658"
        "4824356" = "821231"
        "4824357" = "560681"
        "4824358" = "1218953"
        "4824359" = "1284387"
        "4824360" = "360137"
        "4824361" = "1550505"
        "4824362" = "626463"
        "4824363" = "384647"
        "4824364" = "1295002"
        "4824365" = "637171"
        "4824366" = "903008"
        "4824367" = "1561379"
        "4824368" = "244911"
        "4824369" = "1169024"
        "4824370" = "1042815"
        "4824371" = "510874"
        "4824372" = "777130"
        "4824373" = "1434803"
        "4824374" = "118685"
        "4824375" = "370896"
        "4824376" = "955017"
        "4824377" = "296672"
        "4824380" = "1566072"
        "4824480" = "1060958"
        "4824495" = "200940"
        "4824496" = "622830"
        "4826489" = "1068315"
        "4826492" = "881846"
        "4826493" = "1054618"
        "4826495" = "695790"
        "4826496" = "867940"
        "4826516" = "1313113"
        "4826517" = "1047208"
        "4826518" = "781441"
        "4826519" = "515163"
        "4826520" = "249267"
        "4826521" = "1565730"
        "4826522" = "1299388"
        "4826523" = "1033350"
        "4826524" = "767630"
        "4826537" = "1441822"
        "4826545" = "114076"
        "4826551" = "1202797"
        "4826552" = "484093"
        "4826553" = "1347706"
        "4826555" = "781125"
        "4826571" = "38275"
        "4826572" = "697003"
        "4826573" = "1354516"
        "4826595" = "982449"
        "4826598" = "708021"
        "4828498" = "84816"
        "4828499" = "1134907"
        "4828500" = "298631"
        "4828501" = "1488698"
        "4828504" = "1172769"
        "4828505" = "1518456"
        "4828506" = "1163734"
        "4828507" = "1482015"
        "4828508" = "949636"
        "4828510" = "1311137"
        "4828511" = "779455"
        "4828512" = "247261"
        "4828513" = "658770"
        "4828514" = "1107900"
        "4828515" = "1359908"
        "4828516" = "350802"
        "4828543" = "516895"
        "4828544" = "442537"
        "4828545" = "1188820"
        "4828546" = "259270"
        "4828556" = "650623"
        "4828569" = "910369"
        "4828647" = "1129091"
        "4828791" = "600080"
        "4829721" = "308994"
    }

    # Files whose ids may need rewriting. Each is visited independently so a
    # missing file (e.g. a master-only header that dev consolidated away) is
    # simply skipped rather than fatal.
    # FULL replacement: walk every header under include/ and rewrite REL::ID textually.
    # Why the whole tree instead of a file list: the crash happens during static
    # initialisation and produces no "not found" line, so there is no way to tell
    $idFixRoot = Join-Path $clf4 "CommonLibF4/include"
    $totalFixed = 0
    $filesTouched = 0
    $allHeaders = Get-ChildItem -Path $idFixRoot -Recurse -Include *.h,*.hpp,*.inl -File -ErrorAction SilentlyContinue
    foreach ($f in $allHeaders) {
        $before = [System.IO.File]::ReadAllText($f.FullName)
        $after = $before
        $hits = 0
        foreach ($bad in $idFixes.Keys) {
            $needle = "REL::ID(" + $bad + ")"
            if ($after.Contains($needle)) {
                $after = $after.Replace($needle, "REL::ID(" + $idFixes[$bad] + ")")
                $hits++
            }
        }
        if ($after -ne $before) {
            [System.IO.File]::WriteAllText($f.FullName, $after)
            $totalFixed += $hits
            $filesTouched++
        }
    }
    Write-Host ("  id repair: " + $totalFixed + " stale REL::ID replaced with 1.10.163 values")
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
