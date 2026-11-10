/* SHA-256 (FIPS 180-4), streaming so canonical SQL is never retained. */
typedef struct {
  uint32_t h[8];
  uint64_t bytes;
  unsigned char block[64];
  size_t used;
} sequel_sha256;
static uint32_t sha_rotr(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
static void sha_block(sequel_sha256 *s) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (size_t i = 0; (sequel_work_tick(1), i < 16); i++)
    w[i] = ((uint32_t)s->block[i * 4] << 24) | ((uint32_t)s->block[i * 4 + 1] << 16) |
           ((uint32_t)s->block[i * 4 + 2] << 8) | s->block[i * 4 + 3];
  for (size_t i = 16; (sequel_work_tick(1), i < 64); i++) {
    uint32_t a = w[i - 15], b = w[i - 2];
    w[i] = w[i - 16] + (sha_rotr(a, 7) ^ sha_rotr(a, 18) ^ (a >> 3)) + w[i - 7] +
           (sha_rotr(b, 17) ^ sha_rotr(b, 19) ^ (b >> 10));
  }
  uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
  for (size_t i = 0; (sequel_work_tick(1), i < 64); i++) {
    uint32_t t = h + (sha_rotr(e, 6) ^ sha_rotr(e, 11) ^ sha_rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
    uint32_t u = (sha_rotr(a, 2) ^ sha_rotr(a, 13) ^ sha_rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g;
    g = f;
    f = e;
    e = d + t;
    d = c;
    c = b;
    b = a;
    a = t + u;
  }
  s->h[0] += a;
  s->h[1] += b;
  s->h[2] += c;
  s->h[3] += d;
  s->h[4] += e;
  s->h[5] += f;
  s->h[6] += g;
  s->h[7] += h;
}
static void sha_init(sequel_sha256 *s) {
  *s = (sequel_sha256){
      {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}, 0, {0}, 0};
}
static void sha_update(sequel_sha256 *s, const void *data, size_t n) {
  const unsigned char *p = data;
  s->bytes += n;
  for (size_t i = 0; (sequel_work_tick(1), i < n); i++) {
    s->block[s->used++] = p[i];
    if (s->used == 64) {
      sha_block(s);
      s->used = 0;
    }
  }
}
static void sha_u64(sequel_sha256 *s, uint64_t value) {
  unsigned char b[8];
  for (size_t i = 0; (sequel_work_tick(1), i < 8); i++)
    b[7 - i] = (unsigned char)(value >> (i * 8));
  sha_update(s, b, 8);
}
static void sha_frame(sequel_sha256 *s, unsigned char kind, const char *p, size_t n, bool fold) {
  sha_update(s, &kind, 1);
  sha_u64(s, n);
  if (!fold)
    sha_update(s, p, n);
  else
    for (size_t i = 0; (sequel_work_tick(1), i < n); i++) {
      unsigned char c = (unsigned char)p[i];
      if (c >= 'a' && c <= 'z')
        c -= 32;
      sha_update(s, &c, 1);
    }
}
static void sha_final(sequel_sha256 *s, char out[65]) {
  uint64_t bits = s->bytes * 8;
  unsigned char pad = 0x80, zero = 0;
  sha_update(s, &pad, 1);
  while (s->used != 56)
    sha_update(s, &zero, 1);
  sha_u64(s, bits);
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; (sequel_work_tick(1), i < 32); i++) {
    unsigned char c = (unsigned char)(s->h[i / 4] >> (24 - (i % 4) * 8));
    out[i * 2] = hex[c >> 4];
    out[i * 2 + 1] = hex[c & 15];
  }
  out[64] = 0;
}
