; SPDX-License-Identifier: GPL-3.0-only
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

Unicode true
Name "Awake Launcher"
OutFile "${AWAKE_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\AwakeLauncher"
InstallDirRegKey HKCU "Software\AwakeLauncher" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
AllowSkipFiles off
!define MUI_ICON "${AWAKE_ICON}"
!define MUI_UNICON "${AWAKE_ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install Awake Launcher for your Windows account.$\r$\n$\r$\nClose Awake Launcher before updating. Your accounts and instances are kept separately and will be preserved."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\awakelauncher.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Thai"
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "TradChinese"

VIProductVersion "${AWAKE_VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "Awake Launcher"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "Awake Launcher Windows x64 Setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${AWAKE_VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Awake Launcher Contributors"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Awake Launcher requires 64-bit Windows."
    Quit
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Awake Launcher"
  IfFileExists "$INSTDIR\portable.txt" 0 +3
    MessageBox MB_OK|MB_ICONSTOP "This is a portable installation. Choose a different folder, or update it using the portable ZIP."
    Abort
  SetOutPath "$INSTDIR"
  File /r "${AWAKE_PACKAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\AwakeLauncher" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "DisplayName" "Awake Launcher"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "DisplayVersion" "${AWAKE_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "Publisher" "Awake Launcher Contributors"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "DisplayIcon" "$INSTDIR\awakelauncher.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher" "NoRepair" 1
  CreateShortcut "$SMPROGRAMS\Awake Launcher.lnk" "$INSTDIR\awakelauncher.exe"
SectionEnd

Section "Uninstall"
  SetRegView 64
  ; The generated list removes only shipped files, never account or instance data.
  !include "${AWAKE_DELETE_MANIFEST}"
  Delete "$INSTDIR\uninstall.exe"
  Delete "$SMPROGRAMS\Awake Launcher.lnk"
  DeleteRegKey HKCU "Software\AwakeLauncher"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher"
  RMDir "$INSTDIR"
SectionEnd
