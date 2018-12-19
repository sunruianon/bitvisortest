/*
 * Copyright (c) 2007, 2008 University of Tsukuba
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the University of Tsukuba nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* process VMM calls (hypervisor calls) */

#include "cpu_mmu.h"
#include "current.h"
#include "initfunc.h"
#include "panic.h"
#include "printf.h"
#include "string.h"
#include "mm.h"
#include "vmmcall.h"
#include "msr-extra.h"

#define VMMCALL_MAX 128
#define VMMCALL_NAME_MAXLEN 256

static int n_vmmcall;
static struct {
	char *name;
	vmmcall_func_t func;
} vmmcall_data[VMMCALL_MAX];

/* get a number for VMM call */
/* INPUT: EBX=virtual address of a name of a VMM call (in 256 bytes) */
/* OUTPUT: EAX=a number for the VMM call (0 on error) */
static void
get_vmmcall_number (void)
{
	u32 i;
	char buf[VMMCALL_NAME_MAXLEN];
	ulong nameaddr;

	current->vmctl.read_general_reg (GENERAL_REG_RBX, &nameaddr);
	for (i = 0; i < VMMCALL_NAME_MAXLEN; i++) {
		if (read_linearaddr_b (nameaddr + i, &buf[i])
		    != VMMERR_SUCCESS)
			break;
		if (buf[i] == '\0')
			goto copy_ok;
	}
	current->vmctl.write_general_reg (GENERAL_REG_RAX, 0);
	return;
copy_ok:
	for (i = 0; i < n_vmmcall; i++) {
		if (strcmp (vmmcall_data[i].name, buf) == 0)
			goto found;
	}
	current->vmctl.write_general_reg (GENERAL_REG_RAX, 0);
	return;
found:
	current->vmctl.write_general_reg (GENERAL_REG_RAX, i);
	return;
}

/* handling a VMM call instruction */
void
vmmcall (void)
{
	ulong cmd;

	current->vmctl.read_general_reg (GENERAL_REG_RAX, &cmd);
	if (cmd < VMMCALL_MAX && vmmcall_data[cmd].func)
		vmmcall_data[cmd].func ();
	else
		current->vmctl.write_general_reg (GENERAL_REG_RAX, 0);
}

/* register a VMM call */
void
vmmcall_register (char *name, vmmcall_func_t func)
{
	if (n_vmmcall >= VMMCALL_MAX)
		panic ("Too many vmmcall_register.");
	vmmcall_data[n_vmmcall].name = name;
	vmmcall_data[n_vmmcall].func = func;
	n_vmmcall++;
}


static void
set_lbrbuf (void)
{
    printf("set_lbrbuf.\n") ;

	ulong physaddr;
    ulong sz;

    current->vmctl.read_general_reg (GENERAL_REG_RBX, &physaddr);
    current->vmctl.read_general_reg (GENERAL_REG_RCX, &sz);
    printf("phy: %lx, vir: %lx\n", physaddr, sz);
    if (physaddr == 0 || sz == 0)
        return;
    
	if (lbrbuf != NULL) {
		unmapmem (lbrbuf, lbrbufsize);
		lbrbuf = NULL;
	}
    lbrbuf = (ulong*)mapmem_gphys (physaddr, sz, MAPMEM_WRITE);
    lbrbufsize = sz;
    lbrbuf_idx = 0;
    lbrbuf_maxidx = lbrbufsize/sizeof(u64);

    printf("phy: %lx, vir: %lx\n", physaddr, (ulong)lbrbuf);
}

static void
unset_lbrbuf (void)
{
    printf("unset_lbrbuf.\n") ;

	if (lbrbuf != NULL) {
		unmapmem (lbrbuf, lbrbufsize);
		lbrbuf = NULL;
        lbrbufsize = 0;
	}
}

static void
start_lbrpmu_init (void)
{
    printf("start_lbrpmu_init \n") ;

    start_lbrpmu = 1;
}

static void
stop_lbrpmu_fini (void)
{
    printf("stop_lbrpmu_fini\n") ;

    asm_wrmsr(MSR_IA32_DEBUGCTLMSR, 0x0);
    asm_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
    start_lbrpmu = 0;

    u32 exit_ctls_or, exit_ctls_and;
    u32 entry_ctls_or, entry_ctls_and;
    ulong exit_ctl, entry_ctl;

    /* reset VMCS_VMEXIT ctrls & VMCS_VMENTRY ctrls. */
    asm_rdmsr32 (MSR_IA32_VMX_EXIT_CTLS, &exit_ctls_or, &exit_ctls_and);
    asm_vmread (VMCS_VMEXIT_CTL, &exit_ctl);
    exit_ctl &= ~(VMCS_VMEXIT_CTL_SAVE_DEBUG_CTRL_BIT|VMCS_VMEXIT_CTL_LOAD_PERF_GLOBAL_CTRL_BIT);
    exit_ctl &= exit_ctls_and;
    asm_vmwrite (VMCS_VMEXIT_CTL, exit_ctl);

    asm_rdmsr32 (MSR_IA32_VMX_ENTRY_CTLS, &entry_ctls_or, &entry_ctls_and);
    asm_vmread (VMCS_VMENTRY_CTL, &entry_ctl);
    entry_ctl &= ~(VMCS_VMEXIT_CTL_SAVE_DEBUG_CTRL_BIT|VMCS_VMENTRY_CTL_LOAD_PERF_GLOBAL_CTRL_BIT) ;
    entry_ctl &= entry_ctls_and ;
    asm_vmwrite (VMCS_VMENTRY_CTL, entry_ctl);
}

#define SET_LBRBUF 100
#define UNSET_LBRBUF 101
#define START_LBRPMU 102
#define STOP_LBRPMU 103

void
vmmcall_init (void)
{
    n_vmmcall = 0;
    vmmcall_register ("get_vmmcall_number", get_vmmcall_number);
    call_initfunc ("vmmcal");

    lbrbuf = NULL;
    start_lbrpmu = 0;
    vmmcall_data[SET_LBRBUF].func = set_lbrbuf;
    vmmcall_data[UNSET_LBRBUF].func = unset_lbrbuf;
    vmmcall_data[START_LBRPMU].func = start_lbrpmu_init;
    vmmcall_data[STOP_LBRPMU].func = stop_lbrpmu_fini;
}

INITFUNC ("paral10", vmmcall_init);
