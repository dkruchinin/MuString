#ifndef __MSTRING_PANIC_H__
#define __MSTRING_PANIC_H__

#define panic(format, args...)                  \
    panic_core(__FUNCTION__, format, ##args)

void panic_core(const char *fname, const char *format, ...);

#endif /* __MSTRING_PANIC_H__ */
