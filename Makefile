APP=build\SystemTelemetry.exe
RES=build\SystemTelemetry.res
SRC=src\main.cpp src\config.cpp src\telemetry.cpp src\gpu_vendor.cpp src\gpu_amd_adl.cpp src\utf8.cpp src\trace.cpp src\vendor\adlx\SDK\ADLXHelper\Windows\Cpp\ADLXHelper.cpp src\vendor\adlx\SDK\Platform\Windows\WinAPIs.cpp
CFLAGS=/nologo /std:c++20 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /Zi /Isrc /Isrc\\vendor\\adlx /Fdbuild\\vc140.pdb
LDFLAGS=user32.lib gdi32.lib pdh.lib iphlpapi.lib dxgi.lib comctl32.lib ws2_32.lib ole32.lib oleaut32.lib

all: $(APP)

$(APP): $(SRC) src\config.h src\config.ini resources\SystemTelemetry.rc resources\resource.h resources\cpu.png resources\gpu.png resources\network.png resources\storage.png resources\time.png
	if not exist build mkdir build
	rc /nologo /fo$(RES) resources\SystemTelemetry.rc
	cl $(CFLAGS) /Fe$(APP) /Fobuild\\ $(SRC) $(RES) /link /PDB:build\SystemTelemetry.pdb /ILK:build\SystemTelemetry.ilk $(LDFLAGS)

clean:
	if exist build rmdir /s /q build
