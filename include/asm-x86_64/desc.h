/* Written 2000 by Andi Kleen */ 
#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <linux/threads.h>
#include <asm/ldt.h>

#ifndef __ASSEMBLY__

#define __TSS_INDEX(n)  ((n)*64)
#define __LDT_INDEX(n)  ((n)*64)

enum { 
	GATE_INTERRUPT = 0xE, 
	GATE_TRAP = 0xF, 	
	GATE_CALL = 0xC,
}; 	

// 16byte gate
struct gate_struct {          
	u16 offset_low;
	u16 segment; 
	unsigned ist : 3, zero0 : 5, type : 5, dpl : 2, p : 1;
	u16 offset_middle;
	u32 offset_high;
	u32 zero1; 
} __attribute__((packed));

// 8 byte segment descriptor
struct desc_struct { 
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 4, s : 1, dpl : 2, p : 1;
	unsigned limit : 4, avl : 1, l : 1, d : 1, g : 1, base2 : 8;
} __attribute__((packed)); 

struct n_desc_struct {
	int a,b;
};

// LDT or TSS descriptor in the GDT. 16 bytes.
struct ldttss_desc { 
	u16 limit0;
	u16 base0;
	unsigned base1 : 8, type : 5, dpl : 2, p : 1;
	unsigned limit1 : 4, zero0 : 3, g : 1, base2 : 8;
	u32 base3;
	u32 zero1; 
} __attribute__((packed)); 


extern struct desc_struct cpu_gdt_table[NR_CPUS][GDT_ENTRIES];

#define PTR_LOW(x) ((unsigned long)(x) & 0xFFFF) 
#define PTR_MIDDLE(x) (((unsigned long)(x) >> 16) & 0xFFFF)
#define PTR_HIGH(x) ((unsigned long)(x) >> 32)

enum { 
	DESC_TSS = 0x9,
	DESC_LDT = 0x2,
}; 

struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

#define __GDT_HEAD_SIZE 64
#define __CPU_DESC_INDEX(x,field) \
	((x) * sizeof(struct per_cpu_gdt) + offsetof(struct per_cpu_gdt, field) + __GDT_HEAD_SIZE)

#define load_TR_desc() asm volatile("ltr %w0"::"r" (GDT_ENTRY_TSS*8))
#define load_LDT_desc() asm volatile("lldt %w0"::"r" (GDT_ENTRY_LDT*8))
#define clear_LDT()  asm volatile("lldt %w0"::"r" (0))

extern struct gate_struct idt_table[]; 

#define copy_16byte(dst,src)  memcpy((dst), (src), 16)

static inline void _set_gate(void *adr, unsigned type, unsigned long func, unsigned dpl, unsigned ist)  
{
	struct gate_struct s; 	
	s.offset_low = PTR_LOW(func); 
	s.segment = __KERNEL_CS;
	s.ist = ist; 
	s.p = 1;
	s.dpl = dpl; 
	s.zero0 = 0;
	s.zero1 = 0; 
	s.type = type; 
	s.offset_middle = PTR_MIDDLE(func); 
	s.offset_high = PTR_HIGH(func); 
	copy_16byte(adr, &s);
} 

static inline void set_intr_gate(int nr, void *func) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 0, 0); 
} 

static inline void set_intr_gate_ist(int nr, void *func, unsigned ist) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 0, ist); 
} 

static inline void set_system_gate(int nr, void *func) 
{ 
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, 0); 
} 

static inline void set_tssldt_descriptor(struct ldttss_desc *dst, unsigned long ptr, unsigned type, 
					 unsigned size) 
{ 
	memset(dst,0,16); 
	dst->limit0 = size & 0xFFFF;
	dst->base0 = PTR_LOW(ptr); 
	dst->base1 = PTR_MIDDLE(ptr) & 0xFF; 
	dst->type = type;
	dst->p = 1; 
	dst->limit1 = (size >> 16) & 0xF;
	dst->base2 = (PTR_MIDDLE(ptr) >> 8) & 0xFF; 
	dst->base3 = PTR_HIGH(ptr); 
}

static inline void set_tss_desc(unsigned cpu, void *addr)
{ 
	set_tssldt_descriptor((void*)&cpu_gdt_table[cpu][GDT_ENTRY_TSS], (unsigned long)addr, DESC_TSS, IO_BITMAP_OFFSET + 4 * IO_BITMAP_SIZE + 3);
} 

static inline void set_ldt_desc(unsigned cpu, void *addr, int size)
{ 
	set_tssldt_descriptor((void*)&cpu_gdt_table[cpu][GDT_ENTRY_LDT], (unsigned long)addr, DESC_LDT, size*8-1);
}	

/*
 * load one particular LDT into the current CPU
 */
static inline void load_LDT (struct mm_struct *mm)
{
	int cpu = smp_processor_id();
	void *segments = mm->context.segments;

	if (!segments) {
		clear_LDT();
		return;
	}
		
	set_ldt_desc(cpu, segments, LDT_ENTRIES);
	load_LDT_desc();
}

#define LDT_entry_a(info) \
	((((info)->base_addr & 0x0000ffff) << 16) | ((info)->limit & 0x0ffff))
#define LDT_entry_b(info) \
	(((info)->base_addr & 0xff000000) | \
	(((info)->base_addr & 0x00ff0000) >> 16) | \
	((info)->limit & 0xf0000) | \
	(((info)->read_exec_only ^ 1) << 9) | \
	((info)->contents << 10) | \
	(((info)->seg_not_present ^ 1) << 15) | \
	((info)->seg_32bit << 22) | \
	((info)->limit_in_pages << 23) | \
	((info)->useable << 20) | \
	((info)->lm << 21) | \
	0x7000)

#define LDT_empty(info) (\
	(info)->base_addr	== 0	&& \
	(info)->limit		== 0	&& \
	(info)->contents	== 0	&& \
	(info)->read_exec_only	== 1	&& \
	(info)->seg_32bit	== 0	&& \
	(info)->limit_in_pages	== 0	&& \
	(info)->seg_not_present	== 1	&& \
	(info)->useable		== 0	&& \
	(info)->lm		== 0)


static inline void load_TLS(struct thread_struct *t, unsigned int cpu)
{
	u64 *gdt = (u64 *)(cpu_gdt_table[cpu] + GDT_ENTRY_TLS_MIN);
	gdt[0] = t->tls_array[0];
	gdt[1] = t->tls_array[1];
	gdt[2] = t->tls_array[2];
}

#endif /* !__ASSEMBLY__ */

#endif
