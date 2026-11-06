# Packages the Quest build into an APK, without Gradle. Run after
# android-build (libmain.so in build-android\bin) and a desktop build that made
# the paks (build\bin\Release\Resources):
#
#   .\android\build-apk.ps1                  # build-android\ZeroSpadesX.apk
#   .\android\build-apk.ps1 -Install         # and adb install it on the headset
#   .\android\build-apk.ps1 -Version 0.3.0   # versionName 0.3.0, versionCode 300
#
# The version is CMakeLists.txt's ZEROSPADES_VERSION_* unless -Version says;
# versionCode is major*10000 + minor*100 + patch, so it rises every release (a
# headset only updates to a higher one).
#
# Steps: SDL's Java activity (from the SDL source vcpkg downloaded) and ours,
# javac'd against android.jar and d8'd to classes.dex; aapt2 links the manifest
# and the paks as assets; the stripped native libraries are added; zipalign;
# signed with a local debug key (made once, in build-android, never committed).
param(
	[string] $Jdk = '',
	[string] $Sdk = "$env:LOCALAPPDATA\Android\Sdk",
	[string] $BuildTools = '34.0.0',
	[string] $Platform = 'android-34',
	[string] $Paks = '',
	[string] $SdlSource = '',
	[switch] $Install,
	# Without it the APK is debuggable, so adb run-as can read the game's log.
	[switch] $Release,
	[string] $Version = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location $repo

if (-not $Jdk) {
	$Jdk = Get-ChildItem 'G:\Projects\tools\jdk17' -Directory -ErrorAction SilentlyContinue |
	  Select-Object -First 1 -ExpandProperty FullName
	if (-not $Jdk) { throw 'No JDK 17: pass -Jdk' }
}
$env:JAVA_HOME = $Jdk
$env:PATH = "$Jdk\bin;$env:PATH"
$bt = "$Sdk\build-tools\$BuildTools"
$androidJar = "$Sdk\platforms\$Platform\android.jar"
$ndk = Get-ChildItem "$Sdk\ndk" -Directory | Select-Object -First 1 -ExpandProperty FullName
$strip = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
foreach ($f in @("$bt\aapt2.exe", "$bt\zipalign.exe", "$bt\d8.bat", "$bt\apksigner.bat", $androidJar, $strip)) {
	if (-not (Test-Path $f)) { throw "Missing $f" }
}
if (-not $Paks) { $Paks = "$repo\build\bin\Release\Resources" }
if (-not $SdlSource) {
	$SdlSource = Get-ChildItem "$repo\..\zerospades-vulkan\vcpkg\downloads" -Filter 'libsdl-org-SDL-release-2*.tar.gz' |
	  Select-Object -First 1 -ExpandProperty FullName
	if (-not $SdlSource) { throw 'No SDL source archive: pass -SdlSource' }
}
$libmain = "$repo\build-android\bin\libmain.so"
$libal = "$repo\build-android\openal-soft\libopenal.so"
foreach ($f in @($libmain, $libal)) { if (-not (Test-Path $f)) { throw "Missing $f (run android-build first)" } }

$work = "$repo\build-android\apk"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force "$work\java", "$work\classes", "$work\dex", "$work\assets\Resources", "$work\lib\arm64-v8a" | Out-Null

Write-Host '== Java'
tar -xzf $SdlSource -C "$work\java" --strip-components=6 '*/android-project/app/src/main/java/org'
if ($LASTEXITCODE -ne 0) { throw 'Extracting SDL''s Java failed' }
Copy-Item -Recurse "$PSScriptRoot\java\com" "$work\java\"
$sources = Get-ChildItem -Recurse "$work\java" -Filter *.java | ForEach-Object FullName
# Against the JDK's own Java 8 API, android.jar on the classpath: SDL's lambdas
# need java.lang.invoke, which android.jar lacks; d8 desugars them for Android.
& "$Jdk\bin\javac.exe" -nowarn -encoding UTF-8 --release 8 -classpath $androidJar `
  -d "$work\classes" @sources 2>&1 | Where-Object { $_ -notmatch 'warning: \[options\]|^1 warning|^Note:' }
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
$classes = Get-ChildItem -Recurse "$work\classes" -Filter *.class | ForEach-Object FullName
& "$bt\d8.bat" --release --min-api 29 --lib $androidJar --output "$work\dex" @classes
if ($LASTEXITCODE -ne 0) { throw 'd8 failed' }

Write-Host '== Assets and libraries'
$pakFiles = Get-ChildItem $Paks -Filter *.pak
if (-not $pakFiles) { throw "No paks in $Paks" }
$pakFiles | Copy-Item -Destination "$work\assets\Resources"
# The game copies these out of the APK when this list differs from the one it
# kept (Main.cpp): a name and a fingerprint a line, so a changed pak of the
# same size is still noticed.
$index = $pakFiles | ForEach-Object { "{0} {1}" -f $_.Name, (Get-FileHash $_.FullName -Algorithm SHA1).Hash.ToLower() }
Set-Content -Path "$work\assets\Resources\index.txt" -Value ($index -join "`n") -NoNewline -Encoding ascii
& $strip --strip-unneeded -o "$work\lib\arm64-v8a\libmain.so" $libmain
& $strip --strip-unneeded -o "$work\lib\arm64-v8a\libopenal.so" $libal

Write-Host '== Package'
if (-not $Version) {
	$cm = Get-Content "$repo\CMakeLists.txt" -Raw
	$Version = (@('MAJOR', 'MINOR', 'PATCH') | ForEach-Object {
		if ($cm -notmatch "set\(ZEROSPADES_VERSION_$_ (\d+)\)") { throw "No ZEROSPADES_VERSION_$_" }
		$Matches[1]
	}) -join '.'
}
$Version = $Version.TrimStart('v')
$parts = $Version.Split('.') | ForEach-Object { [int]$_ }
$versionCode = $parts[0] * 10000 + $parts[1] * 100 + $parts[2]
Write-Host "Version $Version (code $versionCode)"
& "$bt\aapt2.exe" link -o "$work\base.apk" --manifest "$PSScriptRoot\AndroidManifest.xml" -I $androidJar `
  -A "$work\assets" --min-sdk-version 29 --target-sdk-version 32 `
  --version-code $versionCode --version-name $Version @(if (-not $Release) { '--debug-mode' })
if ($LASTEXITCODE -ne 0) { throw 'aapt2 link failed' }
# A fresh zip of aapt2's entries, each kept as it was (resources.arsc stays
# stored, which Android 11 and later require), plus classes.dex and the
# libraries. Appending to aapt2's zip instead left headers zipalign warned of.
python -c @"
import zipfile, os, sys
w = sys.argv[1]
with zipfile.ZipFile(os.path.join(w, 'base.apk')) as src, \
     zipfile.ZipFile(os.path.join(w, 'unaligned.apk'), 'w') as dst:
    for info in src.infolist():
        zi = zipfile.ZipInfo(info.filename, info.date_time)
        zi.compress_type = info.compress_type
        zi.external_attr = info.external_attr
        dst.writestr(zi, src.read(info))
    dst.write(os.path.join(w, 'dex', 'classes.dex'), 'classes.dex', zipfile.ZIP_DEFLATED)
    for n in ('libmain.so', 'libopenal.so'):
        dst.write(os.path.join(w, 'lib', 'arm64-v8a', n), 'lib/arm64-v8a/' + n, zipfile.ZIP_DEFLATED)
"@ $work
if ($LASTEXITCODE -ne 0) { throw 'Adding the dex and libraries failed' }
& "$bt\zipalign.exe" -p -f 4 "$work\unaligned.apk" "$work\aligned.apk"
if ($LASTEXITCODE -ne 0) { throw 'zipalign failed' }

# A debug key of this machine's own: the standard debug keystore values, for a
# sideloaded build. A store build would be signed with a real key instead.
$ks = "$repo\build-android\debug.keystore"
if (-not (Test-Path $ks)) {
	& "$Jdk\bin\keytool.exe" -genkeypair -keystore $ks -storepass android -alias androiddebugkey `
	  -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Android Debug,O=Android,C=US' 2>&1 | Out-Null
}
$apk = "$repo\build-android\ZeroSpadesX.apk"
& "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:android --key-pass pass:android --out $apk "$work\aligned.apk"
if ($LASTEXITCODE -ne 0) { throw 'apksigner failed' }
& "$bt\apksigner.bat" verify $apk
if ($LASTEXITCODE -ne 0) { throw 'The APK does not verify' }
Write-Host ("== {0} ({1:N1} MB)" -f $apk, ((Get-Item $apk).Length / 1MB))

if ($Install) {
	& "$Sdk\platform-tools\adb.exe" install -r $apk
}
