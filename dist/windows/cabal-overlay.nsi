# cabal-overlay.nsi — NSIS installer script for the Windows build.
#
# Do not run makensis by hand: dist/windows/build-msys2.sh invokes it
# with the required defines:
#   /DVERSION — release version (parsed from CMakeLists.txt)
#   /DSTAGE   — directory with the bundled exe + DLLs + config + data
#   /DOUTFILE — where to write the setup exe
#
# Layout note: the app resolves config\overlay.toml and data\dungeons.json
# relative to its working directory, so every shortcut sets the working
# directory to $INSTDIR. User settings live in %APPDATA%\cabal-overlay\
# and are NOT touched by the uninstaller.

!define APPNAME "Cabal Overlay"

Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"

!include "MUI2.nsh"
!define MUI_FINISHPAGE_RUN "$INSTDIR\cabal-overlay.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start Cabal Overlay now"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath $INSTDIR
    File /r "${STAGE}\*.*"

    # Working directory of the shortcuts = install dir (see header note).
    SetOutPath $INSTDIR
    CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" \
        "$INSTDIR\cabal-overlay.exe"
    CreateShortcut "$DESKTOP\${APPNAME}.lnk" \
        "$INSTDIR\cabal-overlay.exe"

    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\CabalOverlay" \
        "DisplayName" "${APPNAME}"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\CabalOverlay" \
        "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\CabalOverlay" \
        "DisplayVersion" "${VERSION}"
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    Delete "$DESKTOP\${APPNAME}.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    Delete "$INSTDIR\uninstall.exe"
    # Safe to recurse: user config lives in %APPDATA%, not here.
    RMDir /r "$INSTDIR"
    DeleteRegKey HKLM \
        "Software\Microsoft\Windows\CurrentVersion\Uninstall\CabalOverlay"
SectionEnd
