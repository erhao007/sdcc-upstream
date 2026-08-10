// this file contains BMP chars encoded in UTF-8
#include <stdio.h>
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251)
    /* The large wchar_t literal and its companions overflow the 128-byte
       directly-addressable data area of the small model; keep them in
       xdata on these targets. */
    static __xdata char hello_world_in_czech[] = "čau, světe";
    static __xdata char hello_world_in_czech_ucn[] = "\u010dau, sv\u011bte";
#else
    char hello_world_in_czech[] = "čau, světe";
    char hello_world_in_czech_ucn[] = "\u010dau, sv\u011bte";
#endif
    if (sizeof(hello_world_in_czech) != sizeof(hello_world_in_czech_ucn)
            || strcmp(hello_world_in_czech, hello_world_in_czech_ucn))
        return -1;

#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251)
    static __xdata wchar_t s[] = L"hello$$你好¢¢世界€€world";
#else
    wchar_t s[] = L"hello$$你好¢¢世界€€world";
#endif
    wchar_t *p;
    for (p = s; *p; p++) printf("%04X ", (unsigned) *p);
    printf("\n");
    return 0;
}
