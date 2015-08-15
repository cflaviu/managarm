
$c_SRCDIR := $(TREE_PATH)/$c/src
$c_GENDIR := $(BUILD_PATH)/$c/gen
$c_OBJDIR := $(BUILD_PATH)/$c/obj
$c_BINDIR := $(BUILD_PATH)/$c/bin

$c_OBJECTS := main.o frigg-glue-hel.o linker.o runtime.o \
	frigg-debug.o frigg-initializer.o frigg-libc.o
$c_OBJECT_PATHS := $(addprefix $($c_OBJDIR)/,$($c_OBJECTS))

$c_TARGETS := all-$c clean-$c $($c_BINDIR)/ld-init.so $($c_BINDIR)

$(info all-$c)

.PHONY: all-$c clean-$c

all-$c: $($c_BINDIR)/ld-init.so

clean-$c:
	rm -f $($d_BINDIR)/ld-init.so $($d_OBJECT_PATHS) $($d_OBJECT_PATHS:%.o=%.d)

$($c_GENDIR) $($c_OBJDIR) $($c_BINDIR):
	mkdir -p $@

$c_CXX = x86_64-managarm-g++

$c_INCLUDES := -I$(TREE_PATH)/frigg/include -I$(TREE_PATH)/bragi/include

$c_CXXFLAGS := $(CXXFLAGS) $($c_INCLUDES)
$c_CXXFLAGS += -std=c++1y -Wall -ffreestanding -fno-exceptions -fno-rtti
$c_CXXFLAGS += -fpic -fvisibility=hidden

$c_AS := x86_64-managarm-as

$c_LD := x86_64-managarm-ld
$c_LDFLAGS := -shared

$($c_GENDIR)/frigg-%.cpp: $(TREE_PATH)/frigg/src/%.cpp | $($c_GENDIR)
	install $< $@

$($c_BINDIR)/ld-init.so: $($c_OBJECT_PATHS) | $($c_BINDIR)
	$($d_LD) -o $@ $($d_LDFLAGS) $($d_OBJECT_PATHS)

$($c_OBJDIR)/%.o: $($c_SRCDIR)/%.cpp | $($c_OBJDIR)
	$($d_CXX) -c -o $@ $($d_CXXFLAGS) $<
	$($d_CXX) $($d_CXXFLAGS) -MM -MP -MF $(@:%.o=%.d) -MT "$@" -MT "$(@:%.o=%.d)" $<

$($c_OBJDIR)/%.o: $($c_GENDIR)/%.cpp | $($c_OBJDIR)
	$($d_CXX) -c -o $@ $($d_CXXFLAGS) $<
	$($d_CXX) $($d_CXXFLAGS) -MM -MP -MF $(@:%.o=%.d) -MT "$@" -MT "$(@:%.o=%.d)" $<

$($c_OBJDIR)/%.o: $($c_SRCDIR)/%.s | $($c_SRCDIR)
	$($d_AS) -o $@ $<

-include $($c_OBJECT_PATHS:%.o=%.d)

