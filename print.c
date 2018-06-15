#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "trace.h"

struct reg {
	int size;
	char* name;
};

int32_t read_i32(FILE* f)
{
	int32_t val;
	fread(&val, sizeof(val), 1, f);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	val = __builtin_bswap32(val);
#endif
	return val;
}

int64_t read_i64(FILE* f)
{
	int64_t val;
	fread(&val, sizeof(val), 1, f);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	val = __builtin_bswap64(val);
#endif
	return val;
}

char* read_str(FILE* f)
{
	int length = read_i32(f);
	char* str = (char*) malloc(length + 1);
	fread(str, length, 1, f);
	str[length] = 0;
	return str;
}

int main(int argc, char** argv)
{
	int i;
	int reg_cnt = 0;
	struct reg* regs = NULL;

	FILE* f = fopen(argv[1], "rb");

	if(read_i32(f) != TRACE_MAGIC) {
		printf("invalid magic\n");
		goto error;
	}

	if(read_i32(f) != RECORD_REGISTER_DEFINITION) {
		printf("expected register definition\n");
		goto error;
	}

	int size = read_i32(f);
	size_t pos1 = ftell(f);
	reg_cnt = read_i32(f);
	regs = (struct reg*) malloc(reg_cnt * sizeof(struct reg));
	for(i = 0; i < reg_cnt; i++) {
		regs[i].size = read_i32(f);
		regs[i].name = read_str(f);
	}
	size_t pos2 = ftell(f);
	size_t sz = pos2 - pos1;
	if(sz != size) {
		printf("error: size %lu vs %d\n", sz, size);
		goto error;
	}

	while(!feof(f)) {
		int cmd = read_i32(f);
		size = read_i32(f);
		pos1 = ftell(f);
		switch(cmd) {
			case RECORD_REGISTER_DUMP:
				for(i = 0; i < reg_cnt; i++) {
					struct reg* reg = &regs[i];
					switch(reg->size) {
						case 32: {
							int32_t val = read_i32(f);
							printf("%s=%08x ", reg->name, val);
							break;
						}
						case 64: {
							int64_t val = read_i64(f);
							printf("%s=%016lx ", reg->name, val);
							break;
						}
						case 128: {
							int64_t val1 = read_i64(f);
							int64_t val2 = read_i64(f);
							printf("%s=%016lx%016lx ", reg->name, val1, val2);
							break;
						}
						default:
							printf("unknown size %d\n", reg->size);
							goto error;
					}
				}
				printf("\n");
				break;
			case RECORD_END:
				goto end;
				break;
			default:
				printf("unknown record type %d\n", cmd);
				fseek(f, size, SEEK_CUR);

		}
		pos2 = ftell(f);
		sz = pos2 - pos1;
		if(sz != size) {
			printf("[%d] error: size %lu vs %d\n", cmd, sz, size);
			goto error;
		}
	}

end:
	fclose(f);
	return 0;

error:
	fclose(f);
	return 1;
}
