/* O 360 nao tem execinfo.h. O Midas usa backtrace() so no tratador de falhas,
 * entao aqui ele vira "sem rastro" e o resto compila igual. */
#ifndef XENON_COMPAT_EXECINFO_H
#define XENON_COMPAT_EXECINFO_H
static inline int backtrace(void **buffer, int size) { (void)buffer; (void)size; return 0; }
static inline char **backtrace_symbols(void *const *buffer, int size) { (void)buffer; (void)size; return 0; }
static inline void backtrace_symbols_fd(void *const *buffer, int size, int fd) { (void)buffer; (void)size; (void)fd; }
#endif
