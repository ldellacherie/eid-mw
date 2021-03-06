::Stand alone buildscript for the 64bit MSI installer
::use eid-mw\scripts\windowsbuild_all.bat to build entire middleware

:: Check WiX
:: =========
@echo [INFO] Checking paths
@call "%~dp0..\..\..\scripts\windows\SetPaths.bat"
@if %ERRORLEVEL%==1 goto end


:: Create the revision number
:: ==========================
@call "%~dp0..\..\..\scripts\windows\revision.cmd"


:: Create the MSI installer
:: ========================
@call "%~dp0.\build_msi_eidmw64.cmd"
