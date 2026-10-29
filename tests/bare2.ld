MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000400, LENGTH = 63K
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

SECTIONS
{
  .isr_vector : {
    KEEP(*(.isr_vector))
  } >FLASH

  .text : {
    *(.text)
    *(.text*)
  } >FLASH

  .rodata : {
    KEEP(*(.rodata.markers))
    *(.rodata*)
  } >FLASH
}
