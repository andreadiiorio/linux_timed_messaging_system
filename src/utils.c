/*
 *auxiliary stuff
 */
static inline void _write_cr3(unsigned long val) {
	asm volatile("mov %0,%%cr3": : "r" (val));
}

static inline unsigned long _read_cr3(void) {
	  unsigned long val;
	  asm volatile("mov %%cr3,%0":  "=r" (val) : );
	  return val;
}
