#!/bin/sh

set -eu

#
# Show a welcome message.
#
echo ""
echo "Hello, this is the Suika2 Release Manager."

#
# Check if we run on a macOS host.
#
if [ -z "`uname -a | grep Darwin`" ]; then
    echo "Error: please run on macOS.";
    exit 1;
fi

#
# Check for GNU coreutils.
#
SED='sed'
if [ ! -z "`which gsed`" ]; then
    SED='gsed';
fi
HEAD='head'
if [ ! -z "`which ghead`" ]; then
    HEAD='ghead';
fi

#
# Load credentials from build/.env file.
#
echo "Checking for build/.env credentials."
if [ ! -e .env ]; then
    echo "Error: please create build/.env file."
    exit 1;
fi
FTP_USER=""
FTP_PASSWORD=""
FTP_URL=""
eval `cat .env`;
if [ -z "$FTP_USER" ]; then
    echo "Error: please specify FTP_USER in build/.env";
    exit 1;
fi
if [ -z "$FTP_PASSWORD" ]; then
    echo "Error: please specify FTP_PASSWORD in build/.env";
    exit 1;
fi
if [ -z "$FTP_URL" ]; then
    echo "Error: please specify FTP_URL in build/.env";
    exit 1;
fi

#
# Guess the release version number.
#
VERSION=`grep -a1 '<!-- BEGIN-LATEST-JP -->' ../ChangeLog | tail -n1`
VERSION=`echo $VERSION | cut -d '>' -f 2 | cut -d ' ' -f 1`
VERSION=`echo $VERSION | cut -d '/' -f 2`

#
# Get the release notes.
#
NOTE_JP=`cat ../ChangeLog | awk '/BEGIN-LATEST-JP/,/END-LATEST-JP/' | tail -n +2 | $HEAD -n -1`
NOTE_EN=`cat ../ChangeLog | awk '/BEGIN-LATEST-EN/,/END-LATEST-EN/' | tail -n +2 | $HEAD -n -1`

#
# Do an interactive confirmation.
#
echo "Are you sure you want to release version $VERSION?"
echo ""
echo "[Japanese Note]"
echo "$NOTE_JP"
echo ""
echo "[English Note]"
echo "$NOTE_EN"
echo ""
echo "(press enter to proceed)"
read str

#
# Build "suika.exe".
#
echo "Building suika.exe"
cd engine-windows
rm -f *.o
if [ ! -e libroot ]; then
    ./build-libs.sh;
fi
make -j12
sign.sh suika.exe
cd ..

#
# Build "Suika.app".
#
echo "Building Suika.app (suika-mac.dmg)."
cd engine-macos
rm -f suika-mac.dmg suika-mac-nosign.dmg
make suika-mac.dmg
cp suika-mac.dmg suika-mac-nosign.dmg
codesign --sign 'Developer ID Application: Keiichi TABATA' suika-mac.dmg
cd ..

#
# Build Wasm files.
#
echo "Building Wasm files."
cd engine-wasm
make clean
make
cd ..

#
# Build the iOS source tree.
#
echo "Building iOS source tree."
cd engine-ios
make src
cd ..

#
# Build the Android source tree.
#
echo "Building Android source tree."
cd engine-android
make debug
make src
cd ..
cd pro-android
make
cd ..

#
# Build "suika-pro.exe".
#
echo "Building suika-pro.exe"
cd pro-windows
rm -f *.o
if [ ! -e libroot ]; then
    cp -Rav ../engine-windows/libroot .;
fi
make -j12 VERSION="$VERSION"
sign.sh suika-pro.exe
cd ..

#
# Make an installer for Windows.
#
echo "Creating an installer for Windows."

# /
cp -v pro-windows/suika-pro.exe installer-windows/suika-pro.exe

# /games
rm -rf installer-windows/games
find ../games -name '.*' | xargs rm
mkdir installer-windows/games
cp -Rv ../games/japanese installer-windows/games/
cp -Rv ../games/english installer-windows/games/
cp -Rv ../games/nvl installer-windows/games/
cp -Rv ../games/nvl-tategaki installer-windows/games/
cp -Rv ../games/nvl-en installer-windows/games/

# /tools
rm -rf installer-windows/tools
mkdir -p installer-windows/tools
cp -v engine-windows/suika.exe installer-windows/tools/
cp -v engine-macos/suika-mac.dmg installer-windows/tools/
cp -Rv engine-android/android-src installer-windows/tools/android-src
cp -Rv engine-ios/ios-src installer-windows/tools/ios-src
mkdir -p installer-windows/tools/web
cp -v engine-wasm/html/index.html installer-windows/tools/web/index.html
cp -v engine-wasm/html/index.js installer-windows/tools/web/index.js
cp -v engine-wasm/html/index.wasm installer-windows/tools/web/index.wasm
cp -Rv ../tools/installer installer-windows/tools/installer
cp -v ../tools/snippets/jp-normal/plaintext.code-snippets installer-windows/plaintext.code-snippets.jp
cp -v ../tools/snippets/en-normal/plaintext.code-snippets installer-windows/plaintext.code-snippets.en

# Make an installer
cd installer-windows
make
sign.sh suika2-installer.exe

# Also, make a zip
make zip
cd ..

#
# Build "Suika2 Pro.app".
#
echo "Building Suika2 Pro.app (suika2.dmg)"
cd pro-macos
rm -f suika2.dmg
make
cd ..

#
# Upload.
#
echo "Uploading files."

curl -T "installer-windows/suika2-installer.exe" -u "$FTP_USER:$FTP_PASSWORD" "$FTP_URL/suika2-$VERSION.exe"
curl -T "installer-windows/suika2.zip" -u "$FTP_USER:$FTP_PASSWORD" "$FTP_URL/suika2-$VERSION.zip"
curl -T "pro-macos/suika2.dmg" -u "$FTP_USER:$FTP_PASSWORD" "$FTP_URL/suika2-$VERSION.dmg"
echo "Upload completed."

#
# Update the Web site.
#
echo ""
echo "Updating the Web site."
SAVE_DIR=`pwd`
cd ../doc/web && ./templates.sh && ./version.sh && ./upload.sh && ./push.sh
cd "$SAVE_DIR"

#
# Restore a non-signed dmg for a store release.
#
mv engine-macos/suika-mac-nosign.dmg engine-macos/suika-mac.dmg

#
# Finish.
#
echo "Finished. $VERSION was released!"
