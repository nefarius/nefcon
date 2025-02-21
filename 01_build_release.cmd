@echo off
@setlocal

set MYDIR=%~dp0
pushd "%MYDIR%"

where msbuild > nul 2>&1 && (
    echo Starting build

    msbuild /p:Configuration="Release (WinMain)" /p:Platform=x64 .\NefConUtil.vcxproj -verbosity:minimal
	msbuild /p:Configuration="Release (WinMain)" /p:Platform=ARM64 .\NefConUtil.vcxproj -verbosity:minimal
    msbuild /p:Configuration="Release (WinMain)" /p:Platform=Win32 .\NefConUtil.vcxproj -verbosity:minimal

    msbuild /p:Configuration="Release (Console)" /p:Platform=x64 .\NefConUtil.vcxproj -verbosity:minimal
	msbuild /p:Configuration="Release (Console)" /p:Platform=ARM64 .\NefConUtil.vcxproj -verbosity:minimal
    msbuild /p:Configuration="Release (Console)" /p:Platform=Win32 .\NefConUtil.vcxproj -verbosity:minimal

) || (
    echo MSBuild not found, please make sure you called the "Setup-VS(2022)" snippet before!
)

popd
endlocal
