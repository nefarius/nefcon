@echo off
@setlocal

set MYDIR=%~dp0
pushd "%MYDIR%"

if "%1"=="" (
    echo Error: missing version value argument.
    exit /b 1
)

where vpatch >nul 2>&1

if errorlevel 1 (
    echo Error: vpatch command not found.
    exit /b 1
)

vpatch --stamp-version "%1" --target-file ".\src\NefConUtil.rc" --resource.file-version --resource.product-version

popd
endlocal
