CC = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/cl.exe"
LD = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/link.exe"
INCLUDES = -I"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/include" -I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0/ucrt" -I"C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0/um" -I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.22000.0\shared"
LIBPATHS = /LIBPATH:"C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/lib/x64" /LIBPATH:"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22000.0/um/x64" /LIBPATH:"C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22000.0/ucrt/x64"
SOURCES = patch_kernel_root.cpp analyze_kernel.cpp kallsyms_lookup_name.cpp kallsyms_lookup_name_4_6_0.cpp kernel_symbol_parser.cpp kernel_version_parser.cpp
OBJS = patch_kernel_root.obj analyze_kernel.obj kallsyms_lookup_name.obj kallsyms_lookup_name_4_6_0.obj kernel_symbol_parser.obj kernel_version_parser.obj

CFLAGS=/std:c++17

all: $(OBJS)
	$(LD)	/OUT:patch_kernel.exe	$(OBJS)	$(LIBPATHS)

$(OBJS): $(SOURCES)
	$(CC)	/EHsc	$(CFLAGS)	$(INCLUDES)	/c	$(SOURCES)

clean:
	del *.obj
	del patch_kernel.exe