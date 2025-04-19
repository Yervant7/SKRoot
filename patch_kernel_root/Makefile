CC = cl
LD = link

SOURCES = patch_kernel_root.cpp analyze_kernel.cpp kallsyms_lookup_name.cpp kallsyms_lookup_name_4_6_0.cpp kallsyms_lookup_name_6_1_42.cpp kallsyms_lookup_name_6_1_60.cpp kernel_symbol_parser.cpp kernel_version_parser.cpp
OBJS = $(SOURCES:.cpp=.obj)

CFLAGS = /std:c++17 /EHsc

all: patch_kernel.exe

patch_kernel.exe: $(OBJS)
	$(LD) /OUT:$@ $(OBJS)

%.obj: %.cpp
	$(CC) $(CFLAGS) /c $<

clean:
	del *.obj
	del patch_kernel.exe