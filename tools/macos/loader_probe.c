/* Assessment only: verify the unmodified pcrecomp HLE loader copies PE
 * sections faithfully. Never executes guest code or binds its imports. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "pe_loader.h"
#include "pe_format.h"

const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;

int main(int argc, char **argv) {
    if (argc != 2) return 64;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    if (fseek(f, 0, SEEK_END)) return 2;
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET)) return 2;
    unsigned char *raw = malloc((size_t)n);
    if (!raw || fread(raw, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    pe_image image;
    if (recomp_pe_map(argv[1], &image)) return 2;
    pe_dos_header *dos = (void *)raw;
    pe_nt_headers32 *nt = (void *)(raw + dos->e_lfanew);
    pe_section_header *sec = (void *)((unsigned char *)&nt->OptionalHeader
                                    + nt->FileHeader.SizeOfOptionalHeader);
    int failed = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        size_t size = sec[i].SizeOfRawData;
        if (sec[i].VirtualSize && size > sec[i].VirtualSize)
            size = sec[i].VirtualSize;
        /* Watcom gives .bss a raw size but no file offset: it is zero-fill. */
        if (!size || !sec[i].PointerToRawData) continue;
        if ((size_t)sec[i].PointerToRawData + size > (size_t)n) return 2;
        int same = !memcmp(hle_ptr(image.base + sec[i].VirtualAddress),
                           raw + sec[i].PointerToRawData, size);
        printf("section %.8s: virtual=%u raw=%u contents=%s\n", sec[i].Name,
               sec[i].VirtualSize, sec[i].SizeOfRawData, same ? "MATCH" : "MISMATCH");
        failed += !same;
    }
    printf("host pointer width=%zu; guest base=0x%08X; mismatched sections=%d\n",
           sizeof(void *) * 8, image.base, failed);
    free(raw);
    return failed ? 1 : 0;
}
