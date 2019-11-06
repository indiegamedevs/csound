param
(
    [string]$vsGenerator="Visual Studio 16 2019",
    [switch]$buildStatic=$false,
    [switch]$staticCRT=$false
)

# The default build is to generate dynamic libs that use the CRT dynamically 
if ($buildStatic) {
    $targetTriplet = "x64-windows-static"

    if ($staticCRT == $false) {
        echo "Cannot have dynamic CRT linkage with static Csound build"
        $staticCRT = $true
    }
} 
else {
    if ($staticCRT) {
        $targetTriplet = "x64-windows-crt-mt"
    }
    else {
        $targetTriplet = "x64-windows"
    }
}

echo "Downloading Csound dependencies..."
echo "vsGenerator: $vsGenerator"
echo "Build type: $targetTriplet"

$startTime = (Get-Date).TimeOfDay
$webclient = New-Object System.Net.WebClient
$currentDir = Split-Path $MyInvocation.MyCommand.Path
$cacheDir = $currentDir + "\cache\"
$depsDir = $currentDir + "\deps\"
$stageDir = $currentDir + "\staging\"
$depsBinDir = $depsDir + "bin\"
$depsLibDir = $depsDir + "lib\"
$depsIncDir = $depsDir + "include\"
$csoundDir = $currentDir + "\.."
$vcpkgDir = ""

# Metrics
$vcpkgTiming = 0
$buildTiming = 0
$cmakeTiming = 0

# Find VCPKG from path if it already exists
# Otherwise use the local Csound version that will be installed
$systemVCPKG = $(Get-Command vcpkg -ErrorAction SilentlyContinue).Source
$vcpkgDir = ""

# Test if VCPKG is already installed on system
# Download locally to outside the repo
if ($systemVCPKG)
{
    echo "vcpkg already installed on system, updating"
    $vcpkgDir = Split-Path -Parent $systemVCPKG
    cd $vcpkgDir
    # Update and rebuild vcpkg
    git pull
    git checkout 7b7908b
    bootstrap-vcpkg.bat
    # Remove any outdated packages (they will be installed again below)
    vcpkg remove --outdated --recurse
    vcpkg update # Not really functional it seems yet
    cd $currentDir
}
elseif (Test-Path "..\..\vcpkg")
{
    cd ..\..\vcpkg
    $env:Path += ";" + $(Get-Location)
    $vcpkgDir = $(Get-Location)
    [Environment]::SetEnvironmentVariable("VCPKGDir", $env:vcpkgDir, [EnvironmentVariableTarget]::User)
    echo "vcpkg already installed locally, updating"
    # Update and rebuild vcpkg
    git pull
    git checkout 7b7908b
    bootstrap-vcpkg.bat
    # Remove any outdated packages (they will be installed again below)
    vcpkg remove --outdated --recurse
    vcpkg update
    cd $currentDir
}
else
{
    cd ..\..
    echo "vcpkg missing, downloading and installing..."
    git clone --depth 1 http://github.com/Microsoft/vcpkg.git
    cd vcpkg
    git checkout 7b7908b
    $env:Path += ";" + $(Get-Location)
    $vcpkgDir = $(Get-Location)
    [Environment]::SetEnvironmentVariable("VCPKGDir", $env:vcpkgDir, [EnvironmentVariableTarget]::User)
    bootstrap-vcpkg.bat
    vcpkg integrate install
    cd $currentDir
}

# Generate VCPKG AlwaysAllowDownloads file if needed
New-Item -type file $vcpkgDir\downloads\AlwaysAllowDownloads -errorAction SilentlyContinue | Out-Null

# Download all vcpkg packages available
echo "Downloading VC packages..."

# Download asiosdk and extract before doing portaudio installation
if (-not (Test-Path $vcpkgDir/buildtrees/portaudio/src/asiosdk)) {
    echo "ASIOSDK not installed into VCPKG"
    if (-not (Test-Path .\cache\asiosdk.zip)) {
        Invoke-WebRequest https://www.steinberg.net/asiosdk -OutFile cache/asiosdk.zip
    }
    Expand-Archive -Path cache/asiosdk.zip -DestinationPath $vcpkgDir/buildtrees/portaudio/src -Force
    Move-Item -Path $vcpkgDir/buildtrees/portaudio/src/asiosdk_* -Destination $vcpkgDir/buildtrees/portaudio/src/asiosdk -ErrorAction SilentlyContinue
    # Remove portaudio and it will get rebuilt with asio support in the next step
    vcpkg --triplet $targetTriplet remove portaudio --overlay-triplets=.
}

vcpkg --triplet $targetTriplet install eigen3 fltk zlib libflac libogg libvorbis libsndfile libsamplerate portmidi portaudio liblo hdf5 dirent libstk --overlay-triplets=.

$vcpkgTiming = (Get-Date).TimeOfDay

# Comment for testing to avoid extracting if already done so
rm -Path deps -Force -Recurse -ErrorAction SilentlyContinue
mkdir cache -ErrorAction SilentlyContinue
mkdir deps -ErrorAction SilentlyContinue
mkdir $depsLibDir -ErrorAction SilentlyContinue
mkdir $depsBinDir -ErrorAction SilentlyContinue
mkdir $depsIncDir -ErrorAction SilentlyContinue
mkdir staging -ErrorAction SilentlyContinue

echo "Downloading and installing non-VCPKG packages..."

choco install swig -y
choco upgrade swig -y

choco install winflexbison -y
choco upgrade winflexbison -y

# List of URIs to download and install
$uriList=
"http://ftp.acc.umu.se/pub/gnome/binaries/win64/dependencies/gettext-runtime_0.18.1.1-2_win64.zip",
"http://ftp.acc.umu.se/pub/gnome/binaries/win64/dependencies/pkg-config_0.23-2_win64.zip",
"http://ftp.acc.umu.se/pub/gnome/binaries/win64/dependencies/proxy-libintl-dev_20100902_win64.zip",
"http://ftp.acc.umu.se/pub/gnome/binaries/win64/glib/2.26/glib-dev_2.26.1-1_win64.zip",
"http://ftp.acc.umu.se/pub/gnome/binaries/win64/glib/2.26/glib_2.26.1-1_win64.zip",
"http://download-mirror.savannah.gnu.org/releases/getfem/stable/gmm-5.1.tar.gz"

# Appends this folder location to the 'deps' uri
$destList=
"fluidsynthdeps",
"fluidsynthdeps",
"fluidsynthdeps",
"fluidsynthdeps",
"fluidsynthdeps",
""

# Download list of files to cache folder
for($i=0; $i -lt $uriList.Length; $i++)
{
    $fileName = Split-Path -Leaf $uriList[$i]
    $cachedFile = $cacheDir + $fileName
    if (Test-Path $cachedFile -PathType Leaf)
    {
        echo "Already downloaded file: $fileName"
    }
    else
    {
      echo "Downloading: " $uriList[$i]
      $webclient.DownloadFile($uriList[$i], $cachedFile)
    }
}

# Extract libs to deps directory
for($i=0; $i -lt $uriList.Length; $i++)
{
    $fileName = Split-Path -Leaf $uriList[$i]
    $cachedFile = $cacheDir + $fileName
    $destDir = $depsDir + $destList[$i]
    if ($PSVersionTable.PSVersion.Major -gt 3)
    {
        Expand-Archive $cachedFile -DestinationPath $destDir -Force
    }
    else
    {
        New-Item $destDir -ItemType directory -Force
        Expand-Archive $cachedFile -OutputPath $destDir -Force
    }
    echo "Extracted $fileName to $destDir"
}

# GMM
cd $cacheDir
7z e -y "gmm-5.1.tar.gz"
7z x -y "gmm-5.1.tar"
cd ..
copy ($cacheDir + "gmm-5.1\include\gmm\") -Destination ($depsIncDir + "gmm\") -Force -Recurse
echo "Copied v5.1 gmm headers to deps include directory. Please note, verson 5.1 is REQUIRED, "
echo "later versions do not function as stand-alone, header-file-only libraries."

echo "FluidSynth..."
cd $stageDir

if (Test-Path "fluidsynth")
{
    cd fluidsynth
    git pull
    cd ..
    echo "Fluidsynth already downloaded, updated"
}
else
{
    git clone --depth=1 -b v1.1.10 "https://github.com/FluidSynth/fluidsynth.git"
}

rm -Path fluidsynthbuild -Force -Recurse -ErrorAction SilentlyContinue
mkdir fluidsynthbuild -ErrorAction SilentlyContinue
cd fluidsynthbuild

cmake ..\fluidsynth -G $vsGenerator `
    -DCMAKE_PREFIX_PATH="$depsDir\fluidsynthdeps" `
    -DCMAKE_INCLUDE_PATH="$depsDir\fluidsynthdeps\include\glib-2.0;$depsDir\fluidsynthdeps\lib\glib-2.0\include"

cmake --build . --config Release
copy .\src\Release\fluidsynth.exe -Destination $depsBinDir -Force
copy .\src\Release\fluidsynth.lib -Destination $depsLibDir -Force
copy .\src\Release\libfluidsynth-1.dll -Destination $depsBinDir -Force
copy .\include\fluidsynth.h -Destination $depsIncDir -Force
robocopy ..\fluidsynth\include\fluidsynth $depsIncDir\fluidsynth *.h /s /NJH /NJS
copy .\include\fluidsynth\version.h -Destination $depsIncDir\fluidsynth\version.h -Force

$buildTiming = (Get-Date).TimeOfDay
$endTime = (Get-Date).TimeOfDay
$buildTiming = $buildTiming - $vcpkgTiming
$vcpkgTiming = $vcpkgTiming - $startTime
$duration = $endTime - $startTime

echo "Removed unnecessary files from dependency bin directory"
echo " "
echo "VCPKG duration: $($vcpkgTiming.TotalMinutes) minutes"
echo "Build duration: $($buildTiming.TotalMinutes) minutes"
echo "Total duration: $($duration.TotalMinutes) minutes"
echo "-------------------------------------------------"
echo "Generating Csound Visual Studio solution..."
echo "vsGenerator: $vsGenerator"

$vcpkgCmake = "$vcpkgDir\scripts\buildsystems\vcpkg.cmake"
echo "VCPKG script: '$vcpkgCmake'"

cd $currentDir
mkdir csound-vs -ErrorAction SilentlyContinue
cd csound-vs -ErrorAction SilentlyContinue

$buildSharedLibs = $(if ($buildStatic) { "OFF" } else { "ON" })
$useStaticCRT = $(if ($staticCRT) { "ON" } else { "OFF" })

# Default to Release build type. Note: ReleaseWithDebInfo is broken as VCPKG does not currently support this mode properly
cmake ..\.. -G $vsGenerator `
 -Wno-dev -Wdeprecated `
 -DCMAKE_BUILD_TYPE="Release" `
 -DVCPKG_TARGET_TRIPLET="$targetTriplet" `
 -DCMAKE_TOOLCHAIN_FILE="$vcpkgCmake" `
 -DCMAKE_INSTALL_PREFIX=dist `
 -DCUSTOM_CMAKE="..\Custom-vs.cmake" `
 -DBUILD_SHARED_LIBS=$buildSharedLibs `
 -DSTATIC_CRT=$useStaticCRT
