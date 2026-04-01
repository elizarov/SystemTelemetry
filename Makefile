APP=build\SystemTelemetry.exe
RES=build\SystemTelemetry.res
PROBE=build\GigabyteSivProbe.exe
SRC=src\main.cpp src\config.cpp src\telemetry.cpp src\gpu_vendor.cpp src\gpu_amd_adl.cpp src\board_vendor.cpp src\board_gigabyte_siv.cpp src\utf8.cpp src\trace.cpp src\vendor\adlx\SDK\ADLXHelper\Windows\Cpp\ADLXHelper.cpp src\vendor\adlx\SDK\Platform\Windows\WinAPIs.cpp
CFLAGS=/nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Zi /Isrc /Isrc\\vendor\\adlx /Fdbuild\\vc140.pdb
LDFLAGS=user32.lib gdi32.lib pdh.lib iphlpapi.lib dxgi.lib comctl32.lib ws2_32.lib ole32.lib oleaut32.lib advapi32.lib

all: $(APP) $(PROBE)

!ifndef CSC
!error CSC is not set. Run build.cmd or configure CSC in devenv.cmd.
!endif

$(APP): $(SRC) src\config.h src\config.ini resources\SystemTelemetry.rc resources\resource.h resources\app.ico resources\app_icon.png resources\cpu.png resources\gpu.png resources\network.png resources\storage.png resources\time.png
	if not exist build mkdir build
	rc /nologo /fo$(RES) resources\SystemTelemetry.rc
	cl $(CFLAGS) /Fe$(APP) /Fobuild\\ $(SRC) $(RES) /link /PDB:build\SystemTelemetry.pdb /ILK:build\SystemTelemetry.ilk $(LDFLAGS)

$(PROBE): src\GigabyteSivProbe.cs
	if not exist build mkdir build
	"$(CSC)" /nologo /target:exe /platform:x64 /out:$(PROBE) src\GigabyteSivProbe.cs

clean:
	if exist build rmdir /s /q build
