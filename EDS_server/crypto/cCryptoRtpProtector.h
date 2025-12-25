#pragma once
#include <vector>
namespace Sys {
    namespace Crypto {

        class cCryptoRtpProtector {
        public:
            cCryptoRtpProtector();
            ~cCryptoRtpProtector();

            void fnProtect(std::vector<uint8_t>& packet);
            void fnUnprotect(std::vector<uint8_t>& packet);
        };

        class cCryptoTlsContext {
        public:
            cCryptoTlsContext();
            ~cCryptoTlsContext();

            void fnInit();
        };

    } // namespace Crypto
} // namespace Sys
