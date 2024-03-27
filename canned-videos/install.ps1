$SOURCE_ROOT="https://storage.googleapis.com/sega-kinetoscope/canned-videos"

$FILES=@(
  "BOHEMIAN_RHAPSODY.segavideo",
  "DEVELOPERS_DEVELOPERS.segavideo",
  "GANGNAM_STYLE.segavideo",
  "NEVER_GONNA_GIVE_YOU_UP.segavideo",
  "SHIA_LABEOUF.segavideo",
  "ZOEY_ANN_THE_BOXER.segavideo"
)

$DESTINATION="$env:LOCALAPPDATA\Kinetoscope-Emulation"
New-Item -ItemType Directory -Path "$DESTINATION" -Force

$ProgressPreference = 'SilentlyContinue'
foreach ($FILE in $FILES) {
  if (-not (Test-Path -Path "$DESTINATION/$FILE")) {
    echo "Downloading $FILE..."
    Invoke-WebRequest "$SOURCE_ROOT/$FILE" -OutFile "$DESTINATION/$FILE"
    echo ""
  }
}
