
int
foo(int x[100])
{
#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251)
#  if defined(__SDCC_STACK_AUTO)
	/* Under --stack-auto this function is reentrant, and a non-static
	   storage-classed local is rejected (SDCC error 16). */
	static __xdata
#  else
	__xdata
#  endif
#endif
	int y[100];
	int *p;
	
	y[0] = 2000;
	
	if(x[0] != 1000)
	{
		return 1;
	}
	
	p = x;
	
	if(p[0] != 1000)
	{
		return 2;
	}
	
	p = y;
	
	if(p[0] != 2000)
	{
		return 3;
	}
	
	if(sizeof(x) != sizeof(void*))
	{
		return 4;
	}
	
	if(sizeof(y) <= sizeof(x))
	{
		return 5;
	}
	
	return 0;
}

int
main()
{
#if defined(__SDCC_mcs51) || defined(__SDCC_mcs251)
#  if defined(__SDCC_STACK_AUTO)
	/* Under --stack-auto this function is reentrant, and a non-static
	   storage-classed local is rejected (SDCC error 16). */
	static __xdata
#  else
	__xdata
#  endif
#endif
	int x[100];
	x[0] = 1000;

	return foo(x);
}
