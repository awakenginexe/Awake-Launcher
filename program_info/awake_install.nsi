; SPDX-License-Identifier: GPL-3.0-only
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"
!include "TextFunc.nsh"
Var AwakeUpdateParent
Var AwakeUpdateData
Var AwakeDataMode
Var AwakeDataPath
Var AwakeNormalRadio
Var AwakeCompactRadio
Var AwakeCustomRadio
Var AwakePathInput
Var AwakeBrowseButton

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
Page custom AwakeDataPage AwakeDataPageLeave
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
  ${GetParameters} $0
  ${GetOptions} $0 "/AWAKEUPDATE=" $AwakeUpdateParent
  ${GetOptions} $0 "/AWAKEDATA=" $AwakeUpdateData
  ${If} $AwakeUpdateParent != ""
    ; The launcher starts Setup before quitting; wait until its files are unlocked.
    System::Call 'kernel32::OpenProcess(i 0x100000, i 0, i $AwakeUpdateParent) p.r1'
    ${If} $1 != 0
      System::Call 'kernel32::WaitForSingleObject(p r1, i 60000) i.r2'
      System::Call 'kernel32::CloseHandle(p r1)'
      ${If} $2 != 0
        SetErrorLevel 2
        MessageBox MB_OK|MB_ICONSTOP "Awake Launcher did not close. Close it and run Setup again." /SD IDOK
        Abort
      ${EndIf}
    ${EndIf}
  ${EndIf}
FunctionEnd

Function AwakeLoadDataLocation
  ClearErrors
  FileOpen $0 "$INSTDIR\data-location.txt" r
  ${IfNot} ${Errors}
    FileReadUTF16LE $0 $AwakeDataMode
    FileReadUTF16LE $0 $AwakeDataPath
    FileClose $0
    ${TrimNewLines} $AwakeDataMode $AwakeDataMode
    ${TrimNewLines} $AwakeDataPath $AwakeDataPath
  ${EndIf}
  ${If} $AwakeDataMode == ""
    StrCpy $AwakeDataMode "normal"
  ${EndIf}
FunctionEnd

Function AwakeDataModeChanged
  Pop $0
  ${If} $AwakeDataMode == "custom"
    ${NSD_GetText} $AwakePathInput $AwakeDataPath
  ${EndIf}
  ${NSD_GetState} $AwakeNormalRadio $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $AwakeDataMode "normal"
    ${NSD_SetText} $AwakePathInput "$APPDATA\AwakeLauncher"
    EnableWindow $AwakePathInput 0
    EnableWindow $AwakeBrowseButton 0
  ${Else}
    ${NSD_GetState} $AwakeCompactRadio $0
    ${If} $0 == ${BST_CHECKED}
      StrCpy $AwakeDataMode "compact"
      ${NSD_SetText} $AwakePathInput "$INSTDIR\AwakeLauncherData"
      EnableWindow $AwakePathInput 0
      EnableWindow $AwakeBrowseButton 0
    ${Else}
      StrCpy $AwakeDataMode "custom"
      ${If} $AwakeDataPath == ""
        StrCpy $AwakeDataPath "$APPDATA\AwakeLauncher"
      ${EndIf}
      ${NSD_SetText} $AwakePathInput $AwakeDataPath
      EnableWindow $AwakePathInput 1
      EnableWindow $AwakeBrowseButton 1
    ${EndIf}
  ${EndIf}
FunctionEnd

Function AwakeBrowseData
  Pop $0
  nsDialogs::SelectFolderDialog "Choose launcher data folder" $AwakeDataPath
  Pop $0
  ${If} $0 != "error"
    StrCpy $AwakeDataPath $0
    ${NSD_SetText} $AwakePathInput $0
  ${EndIf}
FunctionEnd

Function AwakeDataPage
  !insertmacro MUI_HEADER_TEXT "Launcher data" "Choose where to store accounts, instances and settings."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == "error"
    Abort
  ${EndIf}
  Call AwakeLoadDataLocation
  ${NSD_CreateRadioButton} 0 0 100% 14u "Normal - AppData\Roaming\AwakeLauncher"
  Pop $AwakeNormalRadio
  ${NSD_CreateRadioButton} 0 22u 100% 14u "Compact - AwakeLauncherData inside the install folder"
  Pop $AwakeCompactRadio
  ${NSD_CreateRadioButton} 0 44u 100% 14u "Custom - Choose a folder"
  Pop $AwakeCustomRadio
  ${NSD_CreateText} 0 68u 78% 14u $AwakeDataPath
  Pop $AwakePathInput
  ${NSD_CreateButton} 80% 68u 20% 14u "Browse..."
  Pop $AwakeBrowseButton
  ${NSD_CreateLabel} 0 94u 100% 38u "Existing data is not moved. To keep your accounts and instances, choose their current folder or copy the data after closing Awake Launcher. Updates keep this choice."
  Pop $0
  ${NSD_OnClick} $AwakeNormalRadio AwakeDataModeChanged
  ${NSD_OnClick} $AwakeCompactRadio AwakeDataModeChanged
  ${NSD_OnClick} $AwakeCustomRadio AwakeDataModeChanged
  ${NSD_OnClick} $AwakeBrowseButton AwakeBrowseData
  ${If} $AwakeDataMode == "compact"
    ${NSD_Check} $AwakeCompactRadio
  ${ElseIf} $AwakeDataMode == "custom"
    ${NSD_Check} $AwakeCustomRadio
  ${Else}
    ${NSD_Check} $AwakeNormalRadio
  ${EndIf}
  Push 0
  Call AwakeDataModeChanged
  nsDialogs::Show
FunctionEnd

Function AwakeDataPageLeave
  ${NSD_GetText} $AwakePathInput $AwakeDataPath
  StrCpy $1 $AwakeDataPath
  System::Call 'shlwapi::PathIsRelativeW(w r1) i.r0'
  ${If} $AwakeDataPath == ""
  ${OrIf} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Choose an absolute data folder."
    Abort
  ${EndIf}
  ClearErrors
  CreateDirectory $AwakeDataPath
  GetTempFileName $1 $AwakeDataPath
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "The selected data folder is not writable. Choose another folder."
    Abort
  ${EndIf}
  Delete $1
FunctionEnd

Function .onInstSuccess
  ${If} $AwakeUpdateParent != ""
    ${If} $AwakeUpdateData != ""
      Exec '"$INSTDIR\awakelauncher.exe" --dir "$AwakeUpdateData"'
    ${Else}
      Exec '"$INSTDIR\awakelauncher.exe"'
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Awake Launcher"
  IfFileExists "$INSTDIR\portable.txt" 0 +3
    MessageBox MB_OK|MB_ICONSTOP "This is a portable installation. Choose a different folder, or update it using the portable ZIP."
    Abort
  SetOutPath "$INSTDIR"
  File /r "${AWAKE_PACKAGE}\*.*"
  ${If} $AwakeDataMode == ""
    Call AwakeLoadDataLocation
  ${EndIf}
  ClearErrors
  FileOpen $0 "$INSTDIR\data-location.txt" w
  FileWriteUTF16LE /BOM $0 "$AwakeDataMode$\r$\n$AwakeDataPath$\r$\n"
  FileClose $0
  ${If} ${Errors}
    SetErrorLevel 3
    MessageBox MB_OK|MB_ICONSTOP "The launcher data location could not be saved. Run Setup again." /SD IDOK
    Abort
  ${EndIf}
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
  Delete "$INSTDIR\data-location.txt"
  Delete "$SMPROGRAMS\Awake Launcher.lnk"
  DeleteRegKey HKCU "Software\AwakeLauncher"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AwakeLauncher"
  RMDir "$INSTDIR"
SectionEnd
