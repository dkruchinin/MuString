#include <arch/types.h>
#include <sync/spinlock.h>
#include <mstring/kconsole.h>
#include <mstring/serial.h>
#include <config.h>

static SPINLOCK_DEFINE(serial_console_lock, "Serial console");

#define LOCK_SERIAL_CONSOLE(is)    spinlock_lock_irqsave(&serial_console_lock,is)
#define UNLOCK_SERIAL_CONSOLE(is)  spinlock_unlock_irqrestore(&serial_console_lock,is)

void serial_init(void)
{
  outb(SERIAL_PORT + 1, 0x00);    // Disable all interrupts
  outb(SERIAL_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
  outb(SERIAL_PORT + 0, 0x01);    // Set divisor to 1 (lo byte) 115200 baud
  outb(SERIAL_PORT + 1, 0x00);    //                  (hi byte)
  outb(SERIAL_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
  outb(SERIAL_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
  outb(SERIAL_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static void serial_cons_init(void);
static void serial_cons_enable(void);
static void serial_cons_disable(void);
static void serial_cons_display_string(const char *s);
static void serial_cons_display_char(const char c);
static void serial_cons_clear(void);

kconsole_t serial_console =
{
  .init = serial_cons_init,
  .enable = serial_cons_enable,
  .disable = serial_cons_disable,
  .clear = serial_cons_clear,
  .display_string = serial_cons_display_string,
  .display_char = serial_cons_display_char,
};

static void serial_cons_enable(void)
{
  long is;

  LOCK_SERIAL_CONSOLE(is);
  serial_init();
  UNLOCK_SERIAL_CONSOLE(is);
  serial_console.is_enabled=true;
}

static void serial_cons_disable(void)
{
  serial_console.is_enabled=false;
}

static void serial_cons_display_string(const char *s)
{
  if( serial_console.is_enabled && s ) {
    long is;

    LOCK_SERIAL_CONSOLE(is);
    while( *s ) {
      serial_write_char(*s++);
    }
    UNLOCK_SERIAL_CONSOLE(is);
  }
}

static void serial_cons_display_char(const char c)
{
  if( serial_console.is_enabled ) {
    long is;

    LOCK_SERIAL_CONSOLE(is);
    serial_write_char(c);
    UNLOCK_SERIAL_CONSOLE(is);
  }
}

static void serial_cons_clear(void)
{
  return;
}

static void serial_cons_init(void)
{
  return;
}
