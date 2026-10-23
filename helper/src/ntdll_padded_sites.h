#ifndef NTDLL_PADDED_SITES_H
#define NTDLL_PADDED_SITES_H
/* Padded short-run split-push sites in Wine ntdll, from
 * results/hw-progress-20261017/ntdll-split-census.json. Preimages are the
 * module's own bytes (ntdll is unobfuscated; hash pinned by the census).
 * The rva is the FUNCTION start, where the padded jump lands. */
#define NTDLL_PADDED_COUNT 4
static const struct {const char *owned_export;unsigned rva;DWORD match_length;
    BYTE preimage[16];DWORD count,span,pad;BYTE widths[8],registers[8];}
    ntdll_padded[NTDLL_PADDED_COUNT]={
    {
        .owned_export="padded_2a",
        .rva=0x14b20,.match_length=10,
        .preimage={0x48,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00,0x56,0x53,0x48,0x81,0xec,0x98,0x01,0x00},
        .count=2,.span=2,.pad=8,
        .widths={1,1,0,0,0,0},
        .registers={6,3,0,0,0,0},
    },
    {
        .owned_export="padded_2b",
        .rva=0x14ce0,.match_length=10,
        .preimage={0x48,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00,0x56,0x53,0x48,0x81,0xec,0x98,0x01,0x00},
        .count=2,.span=2,.pad=8,
        .widths={1,1,0,0,0,0},
        .registers={6,3,0,0,0,0},
    },
    {
        .owned_export="padded_3",
        .rva=0x26a60,.match_length=11,
        .preimage={0x48,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00,0x57,0x56,0x53,0x48,0x83,0xec,0x30,0x65},
        .count=3,.span=3,.pad=8,
        .widths={1,1,1,0,0,0,0},
        .registers={7,6,3,0,0,0,0},
    },
    {
        .owned_export="padded_rtlfree",
        .rva=0x27da0,.match_length=12,
        .preimage={0x48,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00,0x55,0x57,0x56,0x53,0x48,0x81,0xec,0x88},
        .count=4,.span=4,.pad=8,
        .widths={1,1,1,1,0,0,0,0},
        .registers={5,7,6,3,0,0,0,0},
    },
};
#endif
