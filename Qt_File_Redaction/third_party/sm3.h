#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <sstream>
#include <iomanip>

class SM3 {
public:
    // 计算 SM3 哈希，返回 64 位十六进制字符串
    static std::string hash(const std::string& input) {
        uint32_t state[8];
        for (int i = 0; i < 8; ++i) state[i] = IV[i];

        // 消息填充
        std::vector<uint8_t> msg(input.begin(), input.end());
        uint64_t bitLen = (uint64_t)msg.size() * 8;
        msg.push_back(0x80);
        while (msg.size() % 64 != 56) msg.push_back(0x00);
        for (int i = 7; i >= 0; --i) {
            msg.push_back((uint8_t)((bitLen >> (i * 8)) & 0xFF));
        }

        // 逐块压缩
        for (size_t i = 0; i < msg.size(); i += 64) {
            compress(state, &msg[i]);
        }

        // 输出十六进制
        std::stringstream ss;
        for (int i = 0; i < 8; ++i) {
            ss << std::hex << std::setw(8) << std::setfill('0') << state[i];
        }
        return ss.str();
    }

    // 截断哈希（前 N 位十六进制）
    static std::string hashShort(const std::string& input, int hexLength = 16) {
        std::string full = hash(input);
        return hexLength >= (int)full.length() ? full : full.substr(0, hexLength);
    }

private:
    static const uint32_t IV[8];

    static uint32_t rotl(uint32_t x, int n) {
        return (x << n) | (x >> (32 - n));
    }
    static uint32_t P0(uint32_t x) { return x ^ rotl(x, 9) ^ rotl(x, 17); }
    static uint32_t P1(uint32_t x) { return x ^ rotl(x, 15) ^ rotl(x, 23); }

    static void compress(uint32_t state[8], const uint8_t block[64]) {
        uint32_t W[68], W1[64];
        for (int i = 0; i < 16; ++i) {
            W[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                ((uint32_t)block[i * 4 + 2] << 8) | ((uint32_t)block[i * 4 + 3]);
        }
        for (int i = 16; i < 68; ++i) {
            W[i] = P1(W[i - 16] ^ W[i - 9] ^ rotl(W[i - 3], 15)) ^ rotl(W[i - 13], 7) ^ W[i - 6];
        }
        for (int i = 0; i < 64; ++i) W1[i] = W[i] ^ W[i + 4];

        uint32_t A = state[0], B = state[1], C = state[2], D = state[3];
        uint32_t E = state[4], F = state[5], G = state[6], H = state[7];

        for (int j = 0; j < 64; ++j) {
            uint32_t T = (j < 16) ? 0x79cc4519 : 0x7a879d8a;
            uint32_t SS1 = rotl(rotl(A, 12) + E + rotl(T, j % 32), 7);
            uint32_t SS2 = SS1 ^ rotl(A, 12);
            uint32_t FF = (j < 16) ? (A ^ B ^ C) : ((A & B) | (A & C) | (B & C));
            uint32_t GG = (j < 16) ? (E ^ F ^ G) : ((E & F) | (~E & G));
            uint32_t TT1 = FF + D + SS2 + W1[j];
            uint32_t TT2 = GG + H + SS1 + W[j];
            D = C; C = rotl(B, 9); B = A; A = TT1;
            H = G; G = rotl(F, 19); F = E; E = P0(TT2);
        }
        state[0] ^= A; state[1] ^= B; state[2] ^= C; state[3] ^= D;
        state[4] ^= E; state[5] ^= F; state[6] ^= G; state[7] ^= H;
    }
};

// 静态常量定义（放在头文件中会多重定义，但因为 inline 编译器会处理）
// 为了避免多重定义错误，建议在 .cpp 中定义 IV