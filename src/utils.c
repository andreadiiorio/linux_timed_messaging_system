/*
 *auxiliary stuff
 */
inline void _write_cr3(unsigned long val) {
	asm volatile("mov %0,%%cr3": : "r" (val));
}

inline unsigned long _read_cr3(void) {
	  unsigned long val;
	  asm volatile("mov %%cr3,%0":  "=r" (val) : );
	  return val;
}
