#!/usr/bin/env bash
# Packages the Quest build into an APK, without Gradle: build-apk.ps1's steps,
# for Linux and CI. Run after an Android build (libmain.so) and a desktop build
# that made the paks:
#
#   android/build-apk.sh --libs BUILD_ANDROID --paks RESOURCES_DIR \
#       --sdl-source SDL2-x.y.z.tar.gz [--version 0.3.0] --out ZeroSpadesX.apk
#
# The version is CMakeLists.txt's ZEROSPADES_VERSION_* unless --version says.
#
# Signed with the keystore in $ANDROID_KEYSTORE (password $ANDROID_KEYSTORE_PASS,
# key $ANDROID_KEY_ALIAS) when it is set, or else with a throwaway debug key
# made in the work folder: an APK for testing, not for installing over a
# released one (a headset only updates an app signed with the same key).
#
# Needs: ANDROID_HOME (build-tools 34, platform android-34), ANDROID_NDK_HOME,
# a JDK 17 on the PATH, python3.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
libs="" paks="" sdl="" version="" out="" release=0
build_tools="34.0.0" platform="android-34"
while [ $# -gt 0 ]; do
	case "$1" in
		--libs) libs="$2"; shift 2 ;;
		--paks) paks="$2"; shift 2 ;;
		--sdl-source) sdl="$2"; shift 2 ;;
		--version) version="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--release) release=1; shift ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done
if [ -z "$version" ]; then
	version="$(for p in MAJOR MINOR PATCH; do
		sed -n "s/^set(ZEROSPADES_VERSION_$p \([0-9]*\))$/\1/p" "$here/../CMakeLists.txt"
	done | paste -sd.)"
fi
for v in libs paks sdl version out; do
	[ -n "${!v}" ] || { echo "--${v//_/-} is required" >&2; exit 2; }
done

bt="$ANDROID_HOME/build-tools/$build_tools"
android_jar="$ANDROID_HOME/platforms/$platform/android.jar"
strip="$(ls -d "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip | head -n 1)"
libmain="$libs/bin/libmain.so"
libal="$libs/openal-soft/libopenal.so"
for f in "$bt/aapt2" "$bt/zipalign" "$bt/d8" "$bt/apksigner" "$android_jar" "$strip" "$libmain" "$libal"; do
	[ -e "$f" ] || { echo "missing $f" >&2; exit 1; }
done

# 0.3.0 -> 300, 1.12.4 -> 11204: rises with every release, as Android needs
IFS=. read -r major minor patch <<<"${version#v}"
version_code=$(( major * 10000 + minor * 100 + patch ))

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/java" "$work/classes" "$work/dex" "$work/assets/Resources" "$work/lib/arm64-v8a"

echo "== Java"
tar -xzf "$sdl" -C "$work/java" --strip-components=6 --wildcards '*/android-project/app/src/main/java/org'
cp -r "$here/java/com" "$work/java/"
# Against the JDK's own Java 8 API, android.jar on the classpath: SDL's lambdas
# need java.lang.invoke, which android.jar lacks; d8 desugars them for Android.
find "$work/java" -name '*.java' > "$work/sources.txt"
javac -nowarn -encoding UTF-8 --release 8 -classpath "$android_jar" -d "$work/classes" @"$work/sources.txt"
find "$work/classes" -name '*.class' > "$work/classes.txt"
"$bt/d8" --release --min-api 29 --lib "$android_jar" --output "$work/dex" $(cat "$work/classes.txt")

echo "== Assets and libraries"
cp "$paks"/*.pak "$work/assets/Resources/"
# The game copies these out of the APK when this list differs from the one it
# kept (Main.cpp): a name and a fingerprint a line.
(cd "$work/assets/Resources" && for p in *.pak; do printf '%s %s\n' "$p" "$(sha1sum "$p" | cut -d' ' -f1)"; done) |
	head -c -1 > "$work/assets/Resources/index.txt"
"$strip" --strip-unneeded -o "$work/lib/arm64-v8a/libmain.so" "$libmain"
"$strip" --strip-unneeded -o "$work/lib/arm64-v8a/libopenal.so" "$libal"

echo "== Package"
debug_mode=()
[ "$release" = 1 ] || debug_mode=(--debug-mode)
"$bt/aapt2" link -o "$work/base.apk" --manifest "$here/AndroidManifest.xml" -I "$android_jar" \
	-A "$work/assets" --min-sdk-version 29 --target-sdk-version 32 \
	--version-code "$version_code" --version-name "${version#v}" "${debug_mode[@]}"
python3 - "$work" <<'EOF'
import os, sys, zipfile
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
EOF
"$bt/zipalign" -p -f 4 "$work/unaligned.apk" "$work/aligned.apk"

if [ -n "${ANDROID_KEYSTORE:-}" ]; then
	"$bt/apksigner" sign --ks "$ANDROID_KEYSTORE" --ks-key-alias "$ANDROID_KEY_ALIAS" \
		--ks-pass env:ANDROID_KEYSTORE_PASS --key-pass env:ANDROID_KEYSTORE_PASS \
		--out "$out" "$work/aligned.apk"
else
	echo "No ANDROID_KEYSTORE: signing with a throwaway debug key (not for installing over a release)"
	keytool -genkeypair -keystore "$work/debug.keystore" -storepass android -alias androiddebugkey \
		-keypass android -keyalg RSA -keysize 2048 -validity 10000 \
		-dname 'CN=Android Debug,O=Android,C=US' >/dev/null 2>&1
	"$bt/apksigner" sign --ks "$work/debug.keystore" --ks-pass pass:android --key-pass pass:android \
		--out "$out" "$work/aligned.apk"
fi
"$bt/apksigner" verify "$out"
echo "== $out ($(du -h "$out" | cut -f1))"
