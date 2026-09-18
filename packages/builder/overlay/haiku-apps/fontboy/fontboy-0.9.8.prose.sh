# FontBoy: src/alien/FFont/FFont.cpp calls htonl/htons but includes only
# ByteOrder.h and string.h; neither declares them with current Haiku
# headers (<arpa/inet.h> does). Fails natively too.
PATCH()
{
	sed -i '0,/^#include/s//#include <arpa\/inet.h>\n#include/' src/alien/FFont/FFont.cpp
}
