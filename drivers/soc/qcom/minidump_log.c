// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <soc/qcom/minidump.h>
#include <asm/sections.h>
#include <linux/mm.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>
#ifdef VENDOR_EDIT //Fanhong.Kong@PSW.BSP.CHG,add 2017/10/10 for O mini dump
#include <linux/uaccess.h>
#include <asm-generic/irq_regs.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <soc/qcom/memory_dump.h>
#endif/*VENDOR_EDIT*/

static void __init register_log_buf(void)
{
	char **log_bufp;
	uint32_t *log_buf_lenp;
	struct md_region md_entry;

	log_bufp = (char **)kallsyms_lookup_name("log_buf");
	log_buf_lenp = (uint32_t *)kallsyms_lookup_name("log_buf_len");
	if (!log_bufp || !log_buf_lenp) {
		pr_err("Unable to find log_buf by kallsyms!\n");
		return;
	}
	/*Register logbuf to minidump, first idx would be from bss section */
	strlcpy(md_entry.name, "KLOGBUF", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t) (*log_bufp);
	md_entry.phys_addr = virt_to_phys(*log_bufp);
	md_entry.size = *log_buf_lenp;
	if (msm_minidump_add_region(&md_entry))
		pr_err("Failed to add logbuf in Minidump\n");
}

static void register_stack_entry(struct md_region *ksp_entry, u64 sp, u64 size,
				 u32 cpu)
{
	struct page *sp_page;
	struct vm_struct *stack_vm_area = task_stack_vm_area(current);

	ksp_entry->virt_addr = sp;
	ksp_entry->size = size;
	if (stack_vm_area) {
		sp_page = vmalloc_to_page((const void *) sp);
		ksp_entry->phys_addr = page_to_phys(sp_page);
	} else {
		ksp_entry->phys_addr = virt_to_phys((uintptr_t *)sp);
	}

	if (msm_minidump_add_region(ksp_entry))
		pr_err("Failed to add stack of cpu %d in Minidump\n", cpu);
}

static void __init register_kernel_sections(void)
{
	struct md_region ksec_entry;
	char *data_name = "KDATABSS";
	const size_t static_size = __per_cpu_end - __per_cpu_start;
	void __percpu *base = (void __percpu *)__per_cpu_start;
	unsigned int cpu;

	strlcpy(ksec_entry.name, data_name, sizeof(ksec_entry.name));
	ksec_entry.virt_addr = (uintptr_t)_sdata;
	ksec_entry.phys_addr = virt_to_phys(_sdata);
	ksec_entry.size = roundup((__bss_stop - _sdata), 4);
	if (msm_minidump_add_region(&ksec_entry))
		pr_err("Failed to add data section in Minidump\n");

	/* Add percpu static sections */
	for_each_possible_cpu(cpu) {
		void *start = per_cpu_ptr(base, cpu);

		memset(&ksec_entry, 0, sizeof(ksec_entry));
		scnprintf(ksec_entry.name, sizeof(ksec_entry.name),
			"KSPERCPU%d", cpu);
		ksec_entry.virt_addr = (uintptr_t)start;
		ksec_entry.phys_addr = per_cpu_ptr_to_phys(start);
		ksec_entry.size = static_size;
		if (msm_minidump_add_region(&ksec_entry))
			pr_err("Failed to add percpu sections in Minidump\n");
	}
}

static inline bool in_stack_range(u64 sp, u64 base_addr, unsigned int
				  stack_size)
{
	u64 min_addr = base_addr;
	u64 max_addr = base_addr + stack_size;

	return (min_addr <= sp && sp < max_addr);
}

static unsigned int calculate_copy_pages(u64 sp, struct vm_struct *stack_area)
{
	u64 tsk_stack_base = (u64) stack_area->addr;
	u64 offset;
	unsigned int stack_pages, copy_pages;

	if (in_stack_range(sp, tsk_stack_base, get_vm_area_size(stack_area))) {
		offset = sp - tsk_stack_base;
		stack_pages = get_vm_area_size(stack_area) / PAGE_SIZE;
		copy_pages = stack_pages - (offset / PAGE_SIZE);
	} else {
		copy_pages = 0;
	}
	return copy_pages;
}

void dump_stack_minidump(u64 sp)
{
	struct md_region ksp_entry, ktsk_entry;
	u32 cpu = smp_processor_id();
	struct vm_struct *stack_vm_area;
	unsigned int i, copy_pages;

	if (is_idle_task(current))
		return;

	if (sp < KIMAGE_VADDR || sp > -256UL)
		sp = current_stack_pointer;

	/*
	 * Since stacks are now allocated with vmalloc, the translation to
	 * physical address is not a simple linear transformation like it is
	 * for kernel logical addresses, since vmalloc creates a virtual
	 * mapping. Thus, virt_to_phys() should not be used in this context;
	 * instead the page table must be walked to acquire the physical
	 * address of one page of the stack.
	 */
	stack_vm_area = task_stack_vm_area(current);
	if (stack_vm_area) {
		sp &= ~(PAGE_SIZE - 1);
		copy_pages = calculate_copy_pages(sp, stack_vm_area);
		for (i = 0; i < copy_pages; i++) {
			scnprintf(ksp_entry.name, sizeof(ksp_entry.name),
				  "KSTACK%d_%d", cpu, i);
			register_stack_entry(&ksp_entry, sp, PAGE_SIZE, cpu);
			sp += PAGE_SIZE;
		}
	} else {
		sp &= ~(THREAD_SIZE - 1);
		scnprintf(ksp_entry.name, sizeof(ksp_entry.name), "KSTACK%d",
			  cpu);
		register_stack_entry(&ksp_entry, sp, THREAD_SIZE, cpu);
	}

	scnprintf(ktsk_entry.name, sizeof(ktsk_entry.name), "KTASK%d", cpu);
	ktsk_entry.virt_addr = (u64)current;
	ktsk_entry.phys_addr = virt_to_phys((uintptr_t *)current);
	ktsk_entry.size = sizeof(struct task_struct);
	if (msm_minidump_add_region(&ktsk_entry))
		pr_err("Failed to add current task %d in Minidump\n", cpu);
}

#ifdef VENDOR_EDIT //yixue.ge@bsp.drv add for dump cpu contex for minidump
#define CPUCTX_VERSION 1
#define CPUCTX_MAIGC1 0x4D494E49
#define CPUCTX_MAIGC2 (CPUCTX_MAIGC1 + CPUCTX_VERSION)

struct cpudatas{
	struct pt_regs 			pt;
	unsigned int 			regs[32][512];//X0-X30 pc
	unsigned int 			sps[1024];
	unsigned int 			ti[16];//thread_info
	unsigned int			task_struct[1024];
};//16 byte alignment

struct cpuctx{
	unsigned int magic_nubmer1;
	unsigned int magic_nubmer2;
	unsigned int dump_cpus;
	unsigned int reserve;
	struct cpudatas datas[0];
};

static struct cpuctx *Cpucontex_buf = NULL;

//extern int oops_count(void);
extern int panic_count(void);

extern struct pt_regs * get_arm64_cpuregs(struct pt_regs *regs);
/*xing.xiong@BSP.Kernel.Stability, 2018/06/20, Add for dump cpu registers*/
extern int dload_type;
#define SCM_DLOAD_MINIDUMP		0X20

void dumpcpuregs(struct pt_regs *pt_regs)
{
	unsigned int cpu = smp_processor_id();
	struct cpudatas *cpudata = NULL;
	struct pt_regs *regs = pt_regs;
	struct pt_regs regtmp;
	u32	*p;
	unsigned long addr;
	mm_segment_t fs;
	int i,j;

	if (dload_type != SCM_DLOAD_MINIDUMP) {
		return;
	}

	if(Cpucontex_buf == NULL)
		return;

	//if(oops_count() >= 1 && panic_count() >= 1)
	//	return;
	if(Cpucontex_buf->dump_cpus & (0x01 << cpu)){
		return;
	}

	cpudata = &Cpucontex_buf->datas[cpu];

	if(regs != NULL && user_mode(regs)){ //at user mode we must clear pt struct
		//clear flag
		Cpucontex_buf->dump_cpus &=~(0x01 << cpu);
		return;
	}

	if(regs == NULL){
		regs = get_irq_regs();
		if(regs == NULL){
			memset((void*)&regtmp,0,sizeof(struct pt_regs));
			get_arm64_cpuregs(&regtmp);
			regs = &regtmp;
		}
	}

	//set flag
	Cpucontex_buf->dump_cpus |= (0x01 << cpu);

	fs = get_fs();
	set_fs(KERNEL_DS);
	//1.fill pt
	memcpy((void*)&cpudata->pt,(void*)regs,sizeof(struct pt_regs));;
	//2.fill regs
	//2.1 fill x0-x30
	for(i = 0; i < 31; i++){
		addr = regs->regs[i];
		if (!virt_addr_valid(addr) || addr < KIMAGE_VADDR || addr > -256UL)
			continue;
		addr = addr - 256*sizeof(int);
		p = (u32 *)((addr) & ~(sizeof(u32) - 1));
		addr = (unsigned long)p;
		cpudata->regs[i][0] = (unsigned int)(addr&0xffffffff);
		cpudata->regs[i][1] = (unsigned int)((addr>>32)&0xffffffff);
		for(j = 2;j < 256;j++){
			u32	data;
			if (probe_kernel_address(p, data)) {
				break;
			}else{
				cpudata->regs[i][j] = data;
			}
			++p;
			}
	}
	//2.2 fill pc
	addr = regs->pc;
	if (virt_addr_valid(addr) && addr >= KIMAGE_VADDR 
		&& addr < -256UL){
		addr = addr - 256*sizeof(int);
		p = (u32 *)((addr) & ~(sizeof(u32) - 1));
		addr = (unsigned long)p;
		cpudata->regs[i][0] = (unsigned int)(addr&0xffffffff);
		cpudata->regs[i][1] = (unsigned int)((addr>>32)&0xffffffff);
		for(j = 2;j < 256;j++){
			u32	data;
			if (probe_kernel_address(p, data)) {
				break;
			}else{
				cpudata->regs[31][j] = data;
			}
			++p;
		}
	}
	//3. fill sp
	addr = regs->sp;
	if (virt_addr_valid(addr) && addr >= KIMAGE_VADDR && addr < -256UL){
		addr = addr - 512*sizeof(int);
		p = (u32 *)((addr) & ~(sizeof(u32) - 1));
		addr = (unsigned long)p;
		cpudata->sps[0] = (unsigned int)(addr&0xffffffff);
		cpudata->sps[1] = (unsigned int)((addr>>32)&0xffffffff);
		for(j = 2;j < 512;j++){
			u32	data;
			if (probe_kernel_address(p, data)) {
				break;
			}else{
				cpudata->sps[j] = data;
			}
			++p;
		}
	}
	//4. fill task_strcut thread_info
	addr = (unsigned long)current;
	if(virt_addr_valid(addr) && addr >= KIMAGE_VADDR && addr < -256UL){
		cpudata->task_struct[0] = (unsigned int)(addr&0xffffffff);
		cpudata->task_struct[1] = (unsigned int)((addr>>32)&0xffffffff);
		memcpy(&cpudata->task_struct[2],(void*)current,sizeof(struct task_struct));
		addr = (unsigned long)(current->stack);
//Fanhong.Kong@ProDrv.CHG,delete 2019/02/04, for coverity CID 783694 cpudata->ti[2] overflow with thread_info
/*
		if(virt_addr_valid(addr)&& addr >= KIMAGE_VADDR && addr < -256UL){
			cpudata->ti[0] = (unsigned int)(addr&0xffffffff);
			cpudata->ti[1] = (unsigned int)((addr>>32)&0xffffffff);
			memcpy(&cpudata->ti[2],(void*)addr,sizeof(struct thread_info));
		}
*/
	}
	set_fs(fs);
}
EXPORT_SYMBOL(dumpcpuregs);

static void __init register_cpu_contex(void)
{
	int ret;
	struct msm_dump_entry dump_entry;
	struct msm_dump_data *dump_data;

	if (MSM_DUMP_MAJOR(msm_dump_table_version()) > 1) {
		dump_data = kzalloc(sizeof(struct msm_dump_data), GFP_KERNEL);
		if (!dump_data)
			return;
		Cpucontex_buf = ( struct cpuctx *)kzalloc(sizeof(struct cpuctx) +
				sizeof(struct cpudatas)* num_present_cpus(),GFP_KERNEL);

		if (!Cpucontex_buf)
			goto err0;
		//init magic number
		Cpucontex_buf->magic_nubmer1 = CPUCTX_MAIGC1;
		Cpucontex_buf->magic_nubmer2 = CPUCTX_MAIGC2;//version
		Cpucontex_buf->dump_cpus = 0;//version

		strlcpy(dump_data->name, "cpucontex", sizeof(dump_data->name));
		dump_data->addr = virt_to_phys(Cpucontex_buf);
		dump_data->len = sizeof(struct cpuctx) + sizeof(struct cpudatas)* num_present_cpus();
		dump_entry.id = 0;
		dump_entry.addr = virt_to_phys(dump_data);
		ret = msm_dump_data_register(MSM_DUMP_TABLE_APPS, &dump_entry);
		if (ret) {
			pr_err("Registering cpu contex dump region failed\n");
			goto err1;
		}
		return;
err1:
		kfree(Cpucontex_buf);
		Cpucontex_buf=NULL;
err0:
		kfree(dump_data);
	}
}
#endif

static int __init msm_minidump_log_init(void)
{
	register_kernel_sections();
	register_log_buf();
#ifdef VENDOR_EDIT //yixue.ge@bsp.drv add for dump cpu contex for minidump
	register_cpu_contex();
#endif
	return 0;
}
late_initcall(msm_minidump_log_init);
