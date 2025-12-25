#include "cCryptoRtpProtector.h"
#include <iostream>

namespace Sys {
	namespace Crypto {

		cCryptoRtpProtector::cCryptoRtpProtector() {}
		cCryptoRtpProtector::~cCryptoRtpProtector() {}

		void cCryptoRtpProtector::fnProtect(std::vector<uint8_t>&) {}
		void cCryptoRtpProtector::fnUnprotect(std::vector<uint8_t>&) {}

		cCryptoTlsContext::cCryptoTlsContext() {}
		cCryptoTlsContext::~cCryptoTlsContext() {}
		void cCryptoTlsContext::fnInit() {}

	} // namespace Crypto
} // namespace Sys
