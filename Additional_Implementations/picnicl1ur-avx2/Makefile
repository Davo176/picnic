CC=gcc
CFLAGS+=-g -O3
CPPFLAGS+=-I. -Isha3 -DNDEBUG

SOURCES=$(filter-out randomness.c,$(wildcard *.c)) $(wildcard sha3/*.c) $(wildcard sha3/*.s)
EXECUTABLE_APITEST=tests/api_test
EXECUTABLE_NISTKAT=NIST-KATs/PQCgenKAT_sign
EXECUTABLE_EXAMPLE=tests/picnic_test
LIBPICNIC=libpicnic.a
NISTKAT_SOURCES=$(wildcard NIST-KATs/*.c)

all: $(LIBPICNIC) $(EXECUTABLE_APITEST) $(EXECUTABLE_EXAMPLE) $(EXECUTABLE_NISTKAT)

$(LIBPICNIC): $(SOURCES:.c=.o) $(SOURCES:.s=.o)
	ar rcs $@ $^

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

.s.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) -mavx2 $< -o $@

$(EXECUTABLE_NISTKAT): CPPFLAGS+=-DHAVE_RANDOMBYTES
$(EXECUTABLE_NISTKAT): CFLAGS+=-Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-result

$(EXECUTABLE_EXAMPLE): $(EXECUTABLE_EXAMPLE).c $(LIBPICNIC) randomness.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(EXECUTABLE_APITEST): $(EXECUTABLE_APITEST).c $(LIBPICNIC) randomness.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(EXECUTABLE_NISTKAT): $(NISTKAT_SOURCES) $(LIBPICNIC) randomness.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^ -lcrypto -o $@

clean:
	rm -f $(wildcard *.o) $(wildcard sha3/*.o) $(LIBPICNIC) $(EXECUTABLE_APITEST) $(EXECUTABLE_EXAMPLE) $(EXECUTABLE_NISTKAT)
