Name "rcpu (64-bit)"

RequestExecutionLevel highest
SetCompressor /SOLID lzma
SetDateSave off
Unicode true

# Uncomment these lines when investigating reproducibility errors
#SetCompress off
#SetDatablockOptimize off

# General Symbol Definitions
!define REGKEY "SOFTWARE\$(^Name)"
!define COMPANY "rcpu project"
!define URL https://github.com/btcrcpu-max/rcpu

# MUI Symbol Definitions
# !RCPU
!define MUI_ICON "/c/tmp/rcpu-build-security/share/pixmaps/rcpu.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "/c/tmp/rcpu-build-security/share/pixmaps/nsis-wizard-rcpu.bmp"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_RIGHT
!define MUI_HEADERIMAGE_BITMAP "/c/tmp/rcpu-build-security/share/pixmaps/nsis-header-rcpu.bmp"
#! RCPU END
!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_STARTMENUPAGE_REGISTRY_ROOT HKLM
!define MUI_STARTMENUPAGE_REGISTRY_KEY ${REGKEY}
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME StartMenuGroup
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "rcpu"
# !RCPU - removed auto-run on finish
# !define MUI_FINISHPAGE_RUN "$WINDIR\explorer.exe"
# !define MUI_FINISHPAGE_RUN_PARAMETERS $INSTDIR\rcpu-qt.exe
# !RCPU END
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "/c/tmp/rcpu-build-security/share/pixmaps/nsis-wizard.bmp"
!define MUI_UNFINISHPAGE_NOAUTOCLOSE

# Included files
!include Sections.nsh
!include MUI2.nsh
!include x64.nsh

# Variables
Var StartMenuGroup

# Installer pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_STARTMENU Application $StartMenuGroup
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

# Installer languages
!insertmacro MUI_LANGUAGE English

# Installer attributes
# !RCPU
InstallDir $PROGRAMFILES64\RCPU
# !RCPU END
CRCCheck force
XPStyle on
BrandingText " "
ShowInstDetails show
VIProductVersion 1.0.14.0
VIAddVersionKey ProductName "rcpu"
VIAddVersionKey ProductVersion "1.0.14"
VIAddVersionKey CompanyName "${COMPANY}"
VIAddVersionKey CompanyWebsite "${URL}"
VIAddVersionKey FileVersion "1.0.14"
VIAddVersionKey FileDescription "Installer for rcpu"
# !RCPU
VIAddVersionKey LegalCopyright "Copyright (C) 2009-2026 The Bitcoin Core developers and the RCPU Developers"
# !RCPU END
InstallDirRegKey HKCU "${REGKEY}" Path
ShowUninstDetails show

# Installer sections
Section -Main SEC0000
    SetOutPath $INSTDIR
    SetOverwrite on
# !RCPU
    File /c/tmp/rcpu-build-security/release/rcpu-qt.exe
    File /oname=COPYING.txt /c/tmp/rcpu-build-security/COPYING
    File /oname=readme.txt /c/tmp/rcpu-build-security/doc/README_windows_rcpu.txt
    File /c/tmp/rcpu-build-security/share/examples/rcpu.conf
# !RCPU END
    SetOutPath $INSTDIR\share\rpcauth
    File /c/tmp/rcpu-build-security/share/rpcauth/*.*
    SetOutPath $INSTDIR\daemon
# !RCPU
    File /c/tmp/rcpu-build-security/release/rcpud.exe
    File /c/tmp/rcpu-build-security/release/rcpu-cli.exe
    File /c/tmp/rcpu-build-security/release/rcpu-tx.exe
    File /c/tmp/rcpu-build-security/release/rcpu-wallet.exe
# !RCPU END
    SetOutPath $INSTDIR
    WriteRegStr HKCU "${REGKEY}\Components" Main 1
SectionEnd

Section -post SEC0001
    WriteRegStr HKCU "${REGKEY}" Path $INSTDIR
    SetOutPath $INSTDIR
    WriteUninstaller $INSTDIR\uninstall.exe
    !insertmacro MUI_STARTMENU_WRITE_BEGIN Application
    CreateDirectory $SMPROGRAMS\$StartMenuGroup
# !RCPU
    CreateShortcut "$SMPROGRAMS\$StartMenuGroup\$(^Name).lnk" $INSTDIR\rcpu-qt.exe
    CreateShortcut "$DESKTOP\$(^Name).lnk" $INSTDIR\rcpu-qt.exe
# !RCPU END
    CreateShortcut "$SMPROGRAMS\$StartMenuGroup\Uninstall $(^Name).lnk" $INSTDIR\uninstall.exe
    !insertmacro MUI_STARTMENU_WRITE_END
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" DisplayName "$(^Name)"
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" DisplayVersion "1.0.14"
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" Publisher "${COMPANY}"
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" URLInfoAbout "${URL}"
# !RCPU
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" DisplayIcon $INSTDIR\rcpu-qt.exe
# !RCPU END
    WriteRegStr HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" UninstallString $INSTDIR\uninstall.exe
    WriteRegDWORD HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" NoModify 1
    WriteRegDWORD HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)" NoRepair 1
    WriteRegStr HKCR "rcpu" "URL Protocol" ""
# !RCPU
    WriteRegStr HKCR "rcpu" "" "URL:RCPU"
    WriteRegStr HKCR "rcpu\DefaultIcon" "" $INSTDIR\rcpu-qt.exe
    WriteRegStr HKCR "rcpu\shell\open\command" "" '"$INSTDIR\rcpu-qt.exe" "%1"'
# !RCPU END
SectionEnd

# Macro for selecting uninstaller sections
!macro SELECT_UNSECTION SECTION_NAME UNSECTION_ID
    Push $R0
    ReadRegStr $R0 HKCU "${REGKEY}\Components" "${SECTION_NAME}"
    StrCmp $R0 1 0 next${UNSECTION_ID}
    !insertmacro SelectSection "${UNSECTION_ID}"
    GoTo done${UNSECTION_ID}
next${UNSECTION_ID}:
    !insertmacro UnselectSection "${UNSECTION_ID}"
done${UNSECTION_ID}:
    Pop $R0
!macroend

# Uninstaller sections
Section /o -un.Main UNSEC0000
# !RCPU
    Delete /REBOOTOK $INSTDIR\rcpu-qt.exe
# !RCPU END
    Delete /REBOOTOK $INSTDIR\COPYING.txt
    Delete /REBOOTOK $INSTDIR\readme.txt
# !RCPU
    Delete /REBOOTOK $INSTDIR\rcpu.conf
# !RCPU END
    RMDir /r /REBOOTOK $INSTDIR\share
    RMDir /r /REBOOTOK $INSTDIR\daemon
    DeleteRegValue HKCU "${REGKEY}\Components" Main
SectionEnd

Section -un.post UNSEC0001
    DeleteRegKey HKCU "SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\$(^Name)"
    Delete /REBOOTOK "$SMPROGRAMS\$StartMenuGroup\Uninstall $(^Name).lnk"
    Delete /REBOOTOK "$SMPROGRAMS\$StartMenuGroup\$(^Name).lnk"
    Delete /REBOOTOK "$SMPROGRAMS\$StartMenuGroup\rcpu (testnet, 64-bit).lnk"
# !RCPU
    Delete /REBOOTOK "$SMSTARTUP\RCPU.lnk"
# !RCPU END
    Delete /REBOOTOK "$DESKTOP\$(^Name).lnk"
    Delete /REBOOTOK $INSTDIR\uninstall.exe
    # Do NOT delete %APPDATA%\RCPU (datadir is separate from install dir)
    DeleteRegValue HKCU "${REGKEY}" StartMenuGroup
    DeleteRegValue HKCU "${REGKEY}" Path
    DeleteRegKey /IfEmpty HKCU "${REGKEY}\Components"
    DeleteRegKey /IfEmpty HKCU "${REGKEY}"
    DeleteRegKey HKCR "rcpu"
    RmDir /REBOOTOK $SMPROGRAMS\$StartMenuGroup
    RmDir /REBOOTOK $INSTDIR
    Push $R0
    StrCpy $R0 $StartMenuGroup 1
    StrCmp $R0 ">" no_smgroup
no_smgroup:
    Pop $R0
SectionEnd

# Installer functions
Function .onInit
    InitPluginsDir
    ${If} ${RunningX64}
      ; disable registry redirection (enable access to 64-bit portion of registry)
      SetRegView 64
    ${Else}
      MessageBox MB_OK|MB_ICONSTOP "Cannot install 64-bit version on a 32-bit system."
      Abort
    ${EndIf}
    # !RCPU - Check if rcpu-qt / rcpud is already running, abort install if so
    ; detect running GUI: tasklist filtered, findstr exit code 0 = found
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq rcpu-qt.exe" | findstr /i rcpu-qt.exe'
    Pop $R1
    Pop $R0
    StrCmp $R1 "0" app_running
    nsExec::ExecToStack 'cmd /c tasklist /FI "IMAGENAME eq rcpud.exe" | findstr /i rcpud.exe'
    Pop $R1
    Pop $R0
    StrCmp $R1 "0" app_running
    Goto not_running
app_running:
    MessageBox MB_OK|MB_ICONSTOP "RCPU is currently running. Please close it before installing."
    Abort
not_running:
FunctionEnd

# Uninstaller functions
Function un.onInit
    ReadRegStr $INSTDIR HKCU "${REGKEY}" Path
    !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuGroup
    !insertmacro SELECT_UNSECTION Main ${UNSEC0000}
FunctionEnd
