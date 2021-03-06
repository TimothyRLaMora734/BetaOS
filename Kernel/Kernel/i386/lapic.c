//
//  lapic.c
//  BetaOS
//
//  Created by Adam Kopeć on 7/12/16.
//  Copyright © 2016-2017 Adam Kopeć. All rights reserved.
//

#include "lapic.h"
#include "mp.h"
#include "misc_protos.h"
#include "machine_routines.h"
#include "proc_reg.h"
#include "pal.h"
#include "cpuid.h"
#include "cpu_data.h"
#include "vm_map.h"
#include "pmap.h"
#include <stdio.h>

lapic_ops_table_t       *lapic_ops;     /* Lapic operations switch */
static vm_map_offset_t	lapic_pbase;	/* Physical base memory-mapped regs */
static vm_offset_t      lapic_vbase;	/* Virtual base memory-mapped regs */
static i386_intr_func_t lapic_intr_func[LAPIC_FUNC_TABLE_SIZE];
/* true if local APIC was enabled by the OS not by the EFI */
//static bool     lapic_os_enabled = false;
static bool     lapic_errors_masked = false;
//static uint64_t lapic_last_master_error = 0;
//static uint64_t lapic_error_time_threshold = 0;
//static unsigned lapic_master_error_count = 0;
//static unsigned lapic_error_count_threshold = 5;
//static bool     lapic_dont_panic = false;
/* Base vector for local APIC interrupt sources */
int lapic_interrupt_base = LAPIC_DEFAULT_INTERRUPT_BASE;
int		lapic_to_cpu[MAX_LAPICIDS];
int		cpu_to_lapic[MAX_CPUS];

extern bool use_screen_caching;
extern bool experimental;
extern bool early;

void
lapic_cpu_map_init(void) {
	int	i;
    
	for (i = 0; i < MAX_CPUS; i++)
		cpu_to_lapic[i] = -1;
	for (i = 0; i < MAX_LAPICIDS; i++)
		lapic_to_cpu[i] = -1;
}

void
lapic_cpu_map(int apic_id, int cpu) {
	assert(apic_id < MAX_LAPICIDS);
	assert(cpu < MAX_CPUS);
	cpu_to_lapic[cpu] = apic_id;
	lapic_to_cpu[apic_id] = cpu;
}

/*
 * Retrieve the local apic ID a cpu.
 *
 * Returns the local apic ID for the given processor.
 * If the processor does not exist or apic not configured, returns -1.
 */

uint32_t
ml_get_apicid(uint32_t cpu) {
	if(cpu >= (uint32_t)MAX_CPUS)
		return 0xFFFFFFFF;	/* Return -1 if cpu too big */
	
	/* Return the apic ID (or -1 if not configured) */
	return (uint32_t)cpu_to_lapic[cpu];
    
}

uint32_t
ml_get_cpuid(uint32_t lapic_index) {
	if(lapic_index >= (uint32_t)MAX_LAPICIDS)
		return 0xFFFFFFFF;	/* Return -1 if cpu too big */
	
	/* Return the cpu ID (or -1 if not configured) */
	return (uint32_t)lapic_to_cpu[lapic_index];
    
}

#ifdef DEBUG
void
lapic_cpu_map_dump(void) {
	int	i;
	for (i = 0; i < MAX_CPUS; i++) {
		if (cpu_to_lapic[i] == -1)
			continue;
		printf("cpu_to_lapic[%d]: %d\n", i, cpu_to_lapic[i]);
	}
	for (i = 0; i < MAX_LAPICIDS; i++) {
		if (lapic_to_cpu[i] == -1)
			continue;
		printf("lapic_to_cpu[%d]: %d\n", i, lapic_to_cpu[i]);
	}
}
#endif /* DEBUG */

static void
legacy_init(void) {
    __unused int             result;
    __unused vm_map_entry_t  entry;
    __unused vm_map_offset_t lapic_vbase64;
    
    printf("Legacy Init\n");
    
    if (lapic_vbase == 0) {
        lapic_vbase = io_map(lapic_pbase, round_page(LAPIC_SIZE), VM_WIMG_IO);
        //lapic_vbase64 = (vm_map_offset_t)vm_map_min(kernel_map);
    }
    
    LAPIC_WRITE(DFR, LAPIC_DFR_FLAT);
    LAPIC_WRITE(LDR, (get_cpu_number()) << LAPIC_LDR_SHIFT);
}

static uint32_t
legacy_read(lapic_register_t reg) {
    return  *LAPIC_MMIO(reg);
}

static void
legacy_write(lapic_register_t reg, uint32_t value) {
    *LAPIC_MMIO(reg) = value;
}

static uint64_t
legacy_read_icr(void) {
    return (((uint64_t) *LAPIC_MMIO(ICRD)) << 32) | ((uint64_t) *LAPIC_MMIO(ICR));
}

static void
legacy_write_icr(uint32_t dst, uint32_t cmd) {
    *LAPIC_MMIO(ICRD) = dst << LAPIC_ICRD_DEST_SHIFT;
    *LAPIC_MMIO(ICR)  = cmd;
}

static lapic_ops_table_t legacy_ops = {
    legacy_init,
    legacy_read,
    legacy_write,
    legacy_read_icr,
    legacy_write_icr
};

static bool is_x2apic = false;

static void
x2apic_init(void) {
	uint32_t	low;
	uint32_t	high;
 
	rdmsr(MSR_IA32_APIC_BASE, low, high);
	if ((low  & MSR_IA32_APIC_BASE_EXTENDED) == 0)  {
		 low |= MSR_IA32_APIC_BASE_EXTENDED;
		wrmsr(MSR_IA32_APIC_BASE, low, high);
		printf("x2APIC mode enabled\n");
	}
}

static uint32_t
x2apic_read(lapic_register_t reg) {
	uint32_t	low;
	uint32_t	high;
    
	rdmsr(LAPIC_MSR(reg), low, high);
	return low;
}

static void
x2apic_write(lapic_register_t reg, uint32_t value) {
	wrmsr(LAPIC_MSR(reg), value, 0);
}

static uint64_t
x2apic_read_icr(void) {
	return rdmsr64(LAPIC_MSR(ICR));;
}

static void
x2apic_write_icr(uint32_t dst, uint32_t cmd) {
    wrmsr(LAPIC_MSR(ICR), cmd, dst);
}

static lapic_ops_table_t x2apic_ops = {
	x2apic_init,
	x2apic_read,
	x2apic_write,
	x2apic_read_icr,
	x2apic_write_icr
};

extern uint64_t busFreq;

void
lapic_init(void) {
    uint32_t low;
    uint32_t high;
    bool     is_boot_cpu;
    bool     is_lapic_enabled;
    
    rdmsr(MSR_IA32_APIC_BASE, low, high);
    is_boot_cpu      = (low & MSR_IA32_APIC_BASE_BSP)      != 0;
    is_lapic_enabled = (low & MSR_IA32_APIC_BASE_ENABLE)   != 0;
    is_x2apic        = (low & MSR_IA32_APIC_BASE_EXTENDED) != 0;
    lapic_pbase      = (low & MSR_IA32_APIC_BASE_BASE);
    
    DBG("MSR_IA32_APIC_BASE 0x%llx %s %s mode %s\n", lapic_pbase, is_lapic_enabled ? "enabled" : "disabled", is_x2apic ? "extended" : "legacy", is_boot_cpu ? "BSP" : "AP");
    if (!is_boot_cpu || !is_lapic_enabled) {
        panic("Unexpected local APIC state\n");
    }
    
    // If x2APIC is available and not enabled, enable it
    if (!is_x2apic && (cpuid_features() & CPUID_FEATURE_x2APIC)) {
        printf("x2APIC supported and will be enabled\n");
    }
    
    lapic_ops = is_x2apic ? &x2apic_ops : &legacy_ops;
    //lapic_ops = &x2apic_ops;
    
    LAPIC_INIT();
    
    DBG("ID: 0x%x LDR: 0x%x\n", LAPIC_READ(ID), LAPIC_READ(LDR));
    if ((LAPIC_READ(VERSION) & LAPIC_VERSION_MASK) < 0x14) {
        panic("Local APIC version 0x%x, 0x14 or more expected\n", (LAPIC_READ(VERSION) & LAPIC_VERSION_MASK));
    }
    
    lapic_cpu_map_init();
    lapic_cpu_map((LAPIC_READ(ID) >> LAPIC_ID_SHIFT) & LAPIC_ID_MASK, 0);
    current_cpu_datap()->cpu_phys_number = cpu_to_lapic[0];
    DBG("Boot CPU Local APIC ID 0x%x\n", cpu_to_lapic[0]);
    
    LAPIC_WRITE(TPR, 0);
    
    LAPIC_WRITE(SVR, LAPIC_VECTOR(SPURIOUS) | LAPIC_SVR_ENABLE);
    
    LAPIC_WRITE(LVT_TIMER,   LAPIC_VECTOR(TIMER));
    LAPIC_WRITE(LVT_PERFCNT, LAPIC_VECTOR(PERFCNT));
    LAPIC_WRITE(LVT_THERMAL, LAPIC_VECTOR(THERMAL));
    
    if (((cpu_number() == master_cpu) && lapic_errors_masked == false) ||
        (cpu_number() != master_cpu)) {
        // Clear ESR
        LAPIC_WRITE(ERROR_STATUS, 0);
        LAPIC_WRITE(ERROR_STATUS, 0);
        
        LAPIC_WRITE(LVT_ERROR, LAPIC_VECTOR(ERROR));
    }
    
//    pal_sti();
    
    uint32_t vec;
    mp_disable_preemption();
    vec = LAPIC_READ(LVT_TIMER);
    vec &= ~(LAPIC_LVT_MASKED|LAPIC_LVT_TSC_DEADLINE);
    vec |= LAPIC_LVT_PERIODIC;
    LAPIC_WRITE(LVT_TIMER, vec);
    LAPIC_WRITE(TIMER_DIVIDE_CONFIG, divide_by_16);
    mp_enable_preemption();
}

bool
lapic_probe(void) {
    if (cpuid_features() * CPUID_FEATURE_APIC) {
        return true;
    }
    return false;
}

void
lapic_end_of_interrupt(void) {
    LAPIC_WRITE(EOI, 0);
}

void lapic_unmask_perfcnt_interrupt(void) {
    LAPIC_WRITE(LVT_PERFCNT, LAPIC_VECTOR(PERFCNT));
}

void lapic_set_perfcnt_interrupt_mask(bool mask) {
    uint32_t m = (mask ? LAPIC_LVT_MASKED : 0);
    LAPIC_WRITE(LVT_PERFCNT, LAPIC_VECTOR(PERFCNT) | m);
}

void
lapic_set_timer(lapic_timer_count_t initial_count) {
    LAPIC_WRITE(LVT_TIMER, LAPIC_READ(LVT_TIMER) & ~LAPIC_LVT_MASKED);
    LAPIC_WRITE(TIMER_INITIAL_COUNT, initial_count);
}

void
lapic_set_intr_func(int vector, i386_intr_func_t func) {
    if (vector > lapic_interrupt_base) {
        vector -= lapic_interrupt_base;
    }
    
    switch (vector) {
        case LAPIC_NMI_INTERRUPT:
        case LAPIC_INTERPROCESSOR_INTERRUPT:
        case LAPIC_TIMER_INTERRUPT:
        case LAPIC_THERMAL_INTERRUPT:
        case LAPIC_PERFCNT_INTERRUPT:
        case LAPIC_CMCI_INTERRUPT:
        case LAPIC_PM_INTERRUPT:
            lapic_intr_func[vector] = func;
            break;
        default:
            printf("lapic_set_intr_func(%d,%p) invalid vector\n", vector, func);
            break;
    }
}

int
lapic_interrupt( int interrupt_num, __unused x86_saved_state_t *state) {
    interrupt_num -= lapic_interrupt_base;
    
    if (interrupt_num < 0) {
        return 0;
    }
    
    switch (interrupt_num) {
        case LAPIC_TIMER_INTERRUPT:
        case LAPIC_THERMAL_INTERRUPT:
        case LAPIC_INTERPROCESSOR_INTERRUPT:
        case LAPIC_PM_INTERRUPT:
            if (lapic_intr_func[interrupt_num] != NULL)
                (void) (*lapic_intr_func[interrupt_num])(state);
            lapic_end_of_interrupt();
            return 1;
            break;
        case LAPIC_PERFCNT_INTERRUPT:
            /* If a function has been registered, invoke it.  Otherwise,
             * pass up to IOKit.
             */
            if (lapic_intr_func[interrupt_num] != NULL) {
                (void) (*lapic_intr_func[interrupt_num])(state);
                /* Unmask the interrupt since we don't expect legacy users
                 * to be responsible for it.
                 */
                lapic_unmask_perfcnt_interrupt();
                lapic_end_of_interrupt();
                return 1;
                break;
            }
            break;
        case LAPIC_CMCI_INTERRUPT:
            if (lapic_intr_func[interrupt_num] != NULL)
                (void) (*lapic_intr_func[interrupt_num])(state);
            break;
        case LAPIC_ERROR_INTERRUPT:
            LAPIC_WRITE(LVT_ERROR, LAPIC_READ(LVT_ERROR) | LAPIC_LVT_MASKED);
            lapic_end_of_interrupt();
            DBG("LAPIC Error\n");
            return 1;
            break;
        case LAPIC_SPURIOUS_INTERRUPT:
            DBG("SPURIOUS Interrupt\n");
            return 1;
            break;
    }
    
    return 0;
}

void
lapic_send_ipi(int cpu, int vector) {
    bool state;
    if (vector < lapic_interrupt_base) {
        vector += lapic_interrupt_base;
    }
    
    state = ml_set_interrupts_enabled(FALSE);
    
    while (LAPIC_READ_ICR() & LAPIC_ICR_DS_PENDING) {
        cpu_pause();
    }
    
    LAPIC_WRITE_ICR(cpu_to_lapic[cpu], vector | LAPIC_ICR_DM_FIXED);
    
    ml_set_interrupts_enabled(state);
}
