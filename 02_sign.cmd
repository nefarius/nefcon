@echo off
@setlocal

set MYDIR=%~dp0
pushd "%MYDIR%"

wdkwhere run signtool sign /v /n "Nefarius Software Solutions e.U." /tr http://timestamp.digicert.com /fd sha256 /td sha256 ".\bin\x64\*.exe" ".\bin\ARM64\*.exe" ".\bin\x86\*.exe"

popd
endlocal