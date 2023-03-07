::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
:: This batch turns an HLSL file into a C header containing SPIR-V code in an
:: array.
:: This avoids having to load file in unit tests.
::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

echo off
pushd "%~dp0"

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

:: The source HLSL file
set SOURCE=reflection_test_shader.hlsl
:: A temporary file holding the SPIR-V code
set TEMP_SPV=reflection_test_shader
:: The output header file
set OUTPUT_HEADER=reflection_test_shader.h

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

set COMPILER=dxc.exe
set XXD=xxd.exe
:: Compilation options
set COMPILE_OPTIONS=-T ps_6_0 -E main -spirv -fvk-use-dx-layout -fvk-use-dx-position-w -fspv-reflect -fvk-auto-shift-bindings

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

:: Compile from HLSL to SPIR-V
%COMPILER% %COMPILE_OPTIONS% -Fo %TEMP_SPV% %SOURCE%

:: Turns the SPIR-V code into a header
%XXD% -i %TEMP_SPV% %OUTPUT_HEADER%

:: Generate a corresponding SPIR-V ascii file (for debug purpose)
:: %COMPILER% %COMPILE_OPTIONS% %SOURCE% > %TEMP_SPV%.txt

:: Clean up the temporary file
del %TEMP_SPV%

::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

popd

