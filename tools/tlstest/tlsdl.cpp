// A thread_local in a library loaded with dlopen() after the program has
// started: its TLS block is created for each thread on first use.
thread_local int tLate = 1000;

extern "C" int
late_bump()
{
	return ++tLate;
}
