
#include "../kernel.hpp"

namespace traits = frigg::traits;
namespace memory = frigg::memory;
namespace debug = frigg::debug;

namespace thor {

// --------------------------------------------------------
// Debugging functions
// --------------------------------------------------------

void BochsSink::print(char c) {
	frigg::arch_x86::ioOutByte(0xE9, c);
}
void BochsSink::print(const char *str) {
	while(*str != 0)
		frigg::arch_x86::ioOutByte(0xE9, *str++);
}

// --------------------------------------------------------
// ThorRtThreadState
// --------------------------------------------------------

ThorRtThreadState::ThorRtThreadState() {
	memset(&threadTss, 0, sizeof(frigg::arch_x86::Tss64));
	frigg::arch_x86::initializeTss64(&threadTss);

	extendedState = memory::construct<FxSaveState>(*kernelAlloc);
	memset(extendedState, 0, sizeof(FxSaveState));
}

ThorRtThreadState::~ThorRtThreadState() {
	memory::destruct<FxSaveState>(*kernelAlloc, extendedState);
}

void ThorRtThreadState::activate() {
	// set the current general / syscall state pointer
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (&generalState),
			"i" (ThorRtKernelGs::kOffGeneralState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (&syscallState),
			"i" (ThorRtKernelGs::kOffSyscallState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (extendedState),
			"i" (ThorRtKernelGs::kOffExtendedState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1"
			: : "r" (syscallStack + kSyscallStackSize),
			"i" (ThorRtKernelGs::kOffSyscallStackPtr) : "memory" );
	
	// setup the thread's tss segment
	ThorRtCpuSpecific *cpu_specific;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (cpu_specific)
			: "i" (ThorRtKernelGs::kOffCpuSpecific) );
	threadTss.ist1 = cpu_specific->tssTemplate.ist1;
	
	frigg::arch_x86::makeGdtTss64Descriptor(cpu_specific->gdt, 6,
			&threadTss, sizeof(frigg::arch_x86::Tss64));
	asm volatile ( "ltr %w0" : : "r" ( 0x30 ) );
}

void ThorRtThreadState::deactivate() {
	// reset the current general / syscall state pointer
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (nullptr),
			"i" (ThorRtKernelGs::kOffGeneralState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (nullptr),
			"i" (ThorRtKernelGs::kOffSyscallState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (nullptr),
			"i" (ThorRtKernelGs::kOffExtendedState) : "memory" );
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (nullptr),
			"i" (ThorRtKernelGs::kOffSyscallStackPtr) : "memory" );
	
	// setup the tss segment
	ThorRtCpuSpecific *cpu_specific;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (cpu_specific)
			: "i" (ThorRtKernelGs::kOffCpuSpecific) );
	
	frigg::arch_x86::makeGdtTss64Descriptor(cpu_specific->gdt, 6,
			&cpu_specific->tssTemplate, sizeof(frigg::arch_x86::Tss64));
	asm volatile ( "ltr %w0" : : "r" ( 0x30 ) );
}

// --------------------------------------------------------
// ThorRtKernelGs
// --------------------------------------------------------

ThorRtKernelGs::ThorRtKernelGs()
: cpuContext(nullptr), generalState(nullptr), syscallStackPtr(nullptr),
		cpuSpecific(nullptr) { }

// --------------------------------------------------------
// Namespace scope functions
// --------------------------------------------------------

CpuContext *getCpuContext() {
	CpuContext *context;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (context)
			: "i" (ThorRtKernelGs::kOffCpuContext) );
	return context;
}

bool intsAreAllowed() {
	uint32_t flags;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (flags)
			: "i" (ThorRtKernelGs::kOffFlags) );
	return (flags & ThorRtKernelGs::kFlagAllowInts) != 0;
}

void allowInts() {
	uint32_t flags;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (flags)
			: "i" (ThorRtKernelGs::kOffFlags) );
	flags |= ThorRtKernelGs::kFlagAllowInts;
	asm volatile ( "mov %0, %%gs:%c1" : : "r" (flags),
			"i" (ThorRtKernelGs::kOffFlags) );
}

void callOnCpuStack(void (*function) ()) {
	ASSERT(!intsAreEnabled());

	ThorRtCpuSpecific *cpu_specific;
	asm volatile ( "mov %%gs:%c1, %0" : "=r" (cpu_specific)
			: "i" (ThorRtKernelGs::kOffCpuSpecific) );
	
	uintptr_t stack_ptr = (uintptr_t)cpu_specific->cpuStack
			+ ThorRtCpuSpecific::kCpuStackSize;
	
	asm volatile ( "mov %0, %%rsp\n"
			"\tcall *%1\n"
			"\tud2\n" : : "r" (stack_ptr), "r" (function) );
	__builtin_unreachable();
}

extern "C" void syscallStub();

void initializeThisProcessor() {
	auto cpu_specific = memory::construct<ThorRtCpuSpecific>(*thor::kernelAlloc);
	
	// set up the kernel gs segment
	auto kernel_gs = memory::construct<ThorRtKernelGs>(*thor::kernelAlloc);
	kernel_gs->flags = 0;
	kernel_gs->cpuSpecific = cpu_specific;
	kernel_gs->cpuContext = memory::construct<CpuContext>(*kernelAlloc);
	frigg::arch_x86::wrmsr(frigg::arch_x86::kMsrIndexGsBase, (uintptr_t)kernel_gs);

	// setup the gdt
	// note: the tss requires two slots in the gdt
	frigg::arch_x86::makeGdtNullSegment(cpu_specific->gdt, 0);
	// the layout of the next two kernel descriptors is forced by the use of sysret
	frigg::arch_x86::makeGdtCode64SystemSegment(cpu_specific->gdt, 1);
	frigg::arch_x86::makeGdtFlatData32SystemSegment(cpu_specific->gdt, 2);
	// the layout of the next three user-space descriptors is forced by the use of sysret
	frigg::arch_x86::makeGdtNullSegment(cpu_specific->gdt, 3);
	frigg::arch_x86::makeGdtFlatData32UserSegment(cpu_specific->gdt, 4);
	frigg::arch_x86::makeGdtCode64UserSegment(cpu_specific->gdt, 5);
	frigg::arch_x86::makeGdtTss64Descriptor(cpu_specific->gdt, 6, nullptr, 0);

	frigg::arch_x86::Gdtr gdtr;
	gdtr.limit = 8 * 8;
	gdtr.pointer = cpu_specific->gdt;
	asm volatile ( "lgdt (%0)" : : "r"( &gdtr ) );

	asm volatile ( "pushq $0x8\n"
			"\rpushq $.L_reloadCs\n"
			"\rlretq\n"
			".L_reloadCs:" );

	// setup a stack for irqs
	size_t irq_stack_size = 0x10000;
	void *irq_stack_base = thor::kernelAlloc->allocate(irq_stack_size);
	
	// setup the kernel tss
	frigg::arch_x86::initializeTss64(&cpu_specific->tssTemplate);
	cpu_specific->tssTemplate.ist1 = (uintptr_t)irq_stack_base + irq_stack_size;
	
	frigg::arch_x86::makeGdtTss64Descriptor(cpu_specific->gdt, 6,
			&cpu_specific->tssTemplate, sizeof(frigg::arch_x86::Tss64));
	asm volatile ( "ltr %w0" : : "r" ( 0x30 ) );
	
	// setup the idt
	for(int i = 0; i < 256; i++)
		frigg::arch_x86::makeIdt64NullGate(cpu_specific->idt, i);
	setupIdt(cpu_specific->idt);

	frigg::arch_x86::Idtr idtr;
	idtr.limit = 256 * 16;
	idtr.pointer = cpu_specific->idt;
	asm volatile ( "lidt (%0)" : : "r"( &idtr ) );

	// setup the syscall interface
	if((frigg::arch_x86::cpuid(frigg::arch_x86::kCpuIndexExtendedFeatures)[3]
			& frigg::arch_x86::kCpuFlagSyscall) == 0)
		frigg::debug::panicLogger.log() << "CPU does not support the syscall instruction"
				<< frigg::debug::Finish();
	
	uint64_t efer = frigg::arch_x86::rdmsr(frigg::arch_x86::kMsrEfer);
	frigg::arch_x86::wrmsr(frigg::arch_x86::kMsrEfer,
			efer | frigg::arch_x86::kMsrSyscallEnable);

	frigg::arch_x86::wrmsr(frigg::arch_x86::kMsrLstar, (uintptr_t)&syscallStub);
	// user mode cs = 0x18, kernel mode cs = 0x08
	// set user mode rpl bits to work around a qemu bug
	frigg::arch_x86::wrmsr(frigg::arch_x86::kMsrStar,
			(uint64_t(0x1B) << 48) | (uint64_t(0x08) << 32));
	frigg::arch_x86::wrmsr(frigg::arch_x86::kMsrFmask, 0x200); // mask interrupts

	// enable sse support
	uint64_t cr0, cr4;
	asm volatile ( "mov %%cr0, %0" : "=r" (cr0) );
	asm volatile ( "mov %%cr4, %0" : "=r" (cr4) );
	ASSERT((cr0 & 4) == 0); // make sure EM is disabled
	ASSERT((cr0 & 2) == 0); // make sure MP is enabled
	cr4 |= 0x200; // enable OSFXSR
	cr4 |= 0x400; // enable OSXMMEXCPT
	asm volatile ( "mov %0, %%cr4" : : "r" (cr4) );

	initLocalApicPerCpu();
}

// note: these symbols have PHYSICAL addresses!
extern "C" void trampoline();
extern "C" uint32_t trampolineStatus;
extern "C" uint32_t trampolinePml4;
extern "C" uint64_t trampolineStack;

// generated by the linker script
extern "C" uint8_t _trampoline_startLma[];
extern "C" uint8_t _trampoline_endLma[];

bool secondaryBootComplete;
bool finishedBoot;

extern "C" void thorRtSecondaryEntry() {
	// inform the bsp that we do not need the trampoline area anymore
	frigg::volatileWrite<bool>(&secondaryBootComplete, true);

	thor::infoLogger->log() << "Hello world from CPU #"
			<< (getLocalApicId() >> 24) << debug::Finish();	
	initializeThisProcessor();
	allowInts();

	thor::infoLogger->log() << "Start scheduling on AP" << debug::Finish();
	ScheduleGuard schedule_guard(scheduleLock.get());
	doSchedule(traits::move(schedule_guard));
}

void bootSecondary(uint32_t secondary_apic_id) {
	// copy the trampoline code into low physical memory
	uintptr_t trampoline_addr = (uintptr_t)trampoline;
	size_t trampoline_size = (uintptr_t)_trampoline_endLma - (uintptr_t)_trampoline_startLma;
	ASSERT((trampoline_addr % 0x1000) == 0);
	ASSERT((trampoline_size % 0x1000) == 0);
	memcpy(thor::physicalToVirtual(trampoline_addr), _trampoline_startLma, trampoline_size);
	
	size_t trampoline_stack_size = 0x100000;
	void *trampoline_stack_base = thor::kernelAlloc->allocate(trampoline_stack_size);

	// setup the trampoline data area
	auto status_ptr = thor::accessPhysical<uint32_t>((PhysicalAddr)&trampolineStatus);
	auto pml4_ptr = thor::accessPhysical<uint32_t>((PhysicalAddr)&trampolinePml4);
	auto stack_ptr = thor::accessPhysical<uint64_t>((PhysicalAddr)&trampolineStack);
	secondaryBootComplete = false;
	*pml4_ptr = thor::kernelSpace->getPml4();
	*stack_ptr = ((uintptr_t)trampoline_stack_base + trampoline_stack_size);

	raiseInitAssertIpi(secondary_apic_id);
	raiseInitDeassertIpi(secondary_apic_id);
	raiseStartupIpi(secondary_apic_id, trampoline_addr);
	asm volatile ( "" : : : "memory" );
	
	// wait until the ap wakes up
	thor::infoLogger->log() << "Waiting for AP to wake up" << debug::Finish();
	while(frigg::volatileRead<uint32_t>(status_ptr) == 0) {
		frigg::pause();
	}
	
	// allow ap code to initialize the processor
	thor::infoLogger->log() << "AP is booting" << debug::Finish();
	frigg::volatileWrite<uint32_t>(status_ptr, 2);
	
	// wait until the secondary processor completed its boot process
	// we can re-use the trampoline area after this completes
	while(!frigg::volatileRead<bool>(&secondaryBootComplete)) {
		frigg::pause();
	}
	thor::infoLogger->log() << "AP finished booting" << debug::Finish();
}

} // namespace thor

