; TeslaCam Desk - per-user installer (no administrator rights required).
; Build with:
;   python package-installer.py --version 0.2.1 --makensis <path-to-makensis.exe>
; The script must be saved as UTF-8 WITH BOM for the SimpChinese strings to render.

Unicode true

!define APP_NAME "TeslaCam Desk"
!define APP_EXE "TeslaCamManager.exe"
!define PUBLISHER "LocalTools"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TeslaCamDesk"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\dist\TeslaCamDesk-v${VERSION}-windows-x64"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\dist\TeslaCamDesk-v${VERSION}-setup.exe"
!endif
!ifndef UNINSTALL_FILES
  !error "Build with package-installer.py to generate the exact uninstall file list."
!endif

Name "${APP_NAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\TeslaCamDesk"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} Setup"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Leecus and contributors"

!include "MUI2.nsh"
!include "FileFunc.nsh"

; ---- Interface configuration ----
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_UNFINISHPAGE_NOAUTOCLOSE

; Remember the chosen language for the uninstaller.
!define MUI_LANGDLL_REGISTRY_ROOT "HKCU"
!define MUI_LANGDLL_REGISTRY_KEY "${UNINST_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Language"

; ---- Pages (must come BEFORE MUI_LANGUAGE per MUI2 requirement) ----
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; ---- Languages (after page macros, before LangString) ----
; Order = language dialog order; SimpChinese first for this user base.
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---- Localized strings ----
LangString SEC_MAIN ${LANG_ENGLISH} "Application files (required)"
LangString SEC_MAIN ${LANG_SIMPCHINESE} "程序文件（必需）"
LangString SEC_STARTMENU ${LANG_ENGLISH} "Start Menu shortcut"
LangString SEC_STARTMENU ${LANG_SIMPCHINESE} "开始菜单快捷方式"
LangString SEC_DESKTOP ${LANG_ENGLISH} "Desktop shortcut"
LangString SEC_DESKTOP ${LANG_SIMPCHINESE} "桌面快捷方式"
; Uninstaller strings use the "un." prefix.
LangString un.DATA_KEPT ${LANG_ENGLISH} "Your index and thumbnails were KEPT in:\n$LOCALAPPDATA\LocalTools\TeslaCamManager"
LangString un.DATA_KEPT ${LANG_SIMPCHINESE} "您的索引和缩略图已保留在：\n$LOCALAPPDATA\LocalTools\TeslaCamManager"

; ---- Callbacks ----
Function .onInit
  SetShellVarContext current
  IfSilent languageSelected
  !insertmacro MUI_LANGDLL_DISPLAY
  languageSelected:
FunctionEnd

Function un.onInit
  SetShellVarContext current
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

; ---- Install sections ----
Section "$(SEC_MAIN)" SEC_MAIN
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${SOURCE_DIR}\*.*"

  ; Uninstaller + Add/Remove Programs entry (per-user, no admin needed).
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME} ${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "$(SEC_STARTMENU)" SEC_STARTMENU
  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section /o "$(SEC_DESKTOP)" SEC_DESKTOP
  CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\${APP_EXE}" 0
SectionEnd

; ---- Uninstall ----
Section "Uninstall"
  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  ; Remove installed program files only; user data in LocalAppData\LocalTools is untouched.
  Delete "$INSTDIR\Uninstall.exe"
  !include "${UNINSTALL_FILES}"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINST_KEY}"

  IfSilent +2
    MessageBox MB_OK|MB_ICONINFORMATION "$(un.DATA_KEPT)"
SectionEnd
