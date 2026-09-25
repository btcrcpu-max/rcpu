#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <randomx.h>

static void hex2bin(const char* h, unsigned char* out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        std::sscanf(h + 2 * i, "%2x", &v);
        out[i] = (unsigned char)v;
    }
}

// Bitcoin uint256::GetHex() is display order; .begin()/.data() is reversed.
static void hex_gethex_to_internal(const char* h, unsigned char* out) {
    unsigned char tmp[32];
    hex2bin(h, tmp, 32);
    for (int i = 0; i < 32; i++) out[i] = tmp[31 - i];
}

static std::string bin2hex(const unsigned char* p, size_t n) {
    static const char* hexd = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) {
        s[2 * i] = hexd[p[i] >> 4];
        s[2 * i + 1] = hexd[p[i] & 0xf];
    }
    return s;
}

static int check_one(const char* name,
                     const char* input_hex,
                     const char* seed_hex,
                     const char* hash_gethex,
                     const char* commit_gethex) {
    unsigned char input[112], seed[32], hash_raw[32], commit_exp[32];
    hex2bin(input_hex, input, 112);
    hex2bin(seed_hex, seed, 32);
    hex_gethex_to_internal(hash_gethex, hash_raw);
    hex_gethex_to_internal(commit_gethex, commit_exp);

    for (int i = 80; i < 112; i++) {
        if (input[i] != 0) {
            std::printf("%s ERR: rx_input_112 tail not nulled\n", name);
            return 1;
        }
    }

    randomx_flags flags = randomx_get_flags();
    std::printf("%s flags=0x%x\n", name, flags);

    randomx_cache* cache = randomx_alloc_cache(flags);
    if (!cache) { std::printf("%s ERR alloc cache\n", name); return 1; }
    randomx_init_cache(cache, seed, 32);
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);
    if (!vm) { std::printf("%s ERR create vm\n", name); return 1; }

    unsigned char out_hash[RANDOMX_HASH_SIZE];
    randomx_calculate_hash(vm, input, 112, out_hash);

    unsigned char out_cm[RANDOMX_HASH_SIZE];
    randomx_calculate_commitment(input, 112, hash_raw, out_cm);

    int ok_h = std::memcmp(out_hash, hash_raw, 32) == 0;
    int ok_c = std::memcmp(out_cm, commit_exp, 32) == 0;
    std::printf("%s hash      computed=%s MATCH=%s\n", name, bin2hex(out_hash, 32).c_str(), ok_h ? "YES" : "NO");
    std::printf("%s commit    computed=%s MATCH=%s\n", name, bin2hex(out_cm, 32).c_str(), ok_c ? "YES" : "NO");

    randomx_destroy_vm(vm);
    randomx_release_cache(cache);
    return (ok_h && ok_c) ? 0 : 1;
}

int main() {
    const char* seed = "169a4b98b46cdc27a8cb04d1c6049c535bce5c198472e646c2eaa3f47c53d583";
    int rc = 0;
    rc |= check_one(
        "A2000",
        "000000209cfcc00fb7cd9edcf11b731c2861a49265d5a72aebb3d97e98773c361a633a127e0b913e4dcf88c76686c72ea3f645e95971b1eaabff26f67b34117aa1a115a5d2fb9c6affd7081e60022b870000000000000000000000000000000000000000000000000000000000000000",
        seed,
        "1a351ad5985948097a918c0a334420d20b3220d212d9dc2091f0eb57c739dd9b",
        "000004fc216d42ca09da5c98fbd10d43ccb0c6911dfa2b8df22c5dabb9d02b90");
    rc |= check_one(
        "A4000",
        "00000020b5489790f2ae1ea473fb74bcf3b3eb499b650222d03ecb35546bd01ab06d4dac195ab158eeaef81b5977497949da6c77851e8992b076f259c902572d52f5f0bb93ef9f6ac5ba011eb63633b30000000000000000000000000000000000000000000000000000000000000000",
        seed,
        "94f25a23e854947845dc847e2bfec1b7c20bc6cf609ac0de8cdd68ac5cb72c48",
        "0000004da523872966cb1ba5fd2f283220ccce64bb1b192c63065c3749dafa91");
    return rc;
}
