/* Compile the private engine into this test only. This tests standard SHA-256
 * vectors directly without adding a public hash API or linking a second engine. */
#include "../src/sql/main.c"

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      fprintf(stderr, "%d: %s\n", __LINE__, #condition);                                                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int known_vectors(void) {
  struct {
    const char *input, *expected;
  } vectors[] = {{"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
                 {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
                 {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"}};
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
    sequel_sha256 hash;
    char actual[65];
    sha_init(&hash);
    sha_update(&hash, vectors[i].input, strlen(vectors[i].input));
    sha_final(&hash, actual);
    CHECK(!strcmp(actual, vectors[i].expected));
    /* Chunk boundaries cannot change the digest. */
    sha_init(&hash);
    for (size_t k = 0; k < strlen(vectors[i].input); ++k)
      sha_update(&hash, vectors[i].input + k, 1);
    sha_final(&hash, actual);
    CHECK(!strcmp(actual, vectors[i].expected));
  }
  sequel_sha256 hash;
  char actual[65], block[1000];
  memset(block, 'a', sizeof(block));
  sha_init(&hash);
  for (size_t i = 0; i < 1000; ++i)
    sha_update(&hash, block, sizeof(block));
  sha_final(&hash, actual);
  CHECK(!strcmp(actual, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
  return 0;
}

int main(void) { return known_vectors(); }
