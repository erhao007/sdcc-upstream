#include <stdio.h>

int main()
{
#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251)
   static __xdata
#endif
   char Buf[100];
   int Count;

   for (Count = 1; Count <= 20; Count++)
   {
      sprintf(Buf, "->%02d<-\n", Count);
      printf("%s", Buf);
   }

   return 0;
}

/* vim: set expandtab ts=4 sw=3 sts=3 tw=80 :*/
