// A thread_local defined in a shared library and read from the program:
// a TLS descriptor against another module's symbol.
thread_local int tShared = 100;

int
shared_bump()
{
	return ++tShared;
}
