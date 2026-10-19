#include "common.h"

#pragma GCC optimize ("Oz")

#define RN_TABLE        0
#define RD_TABLE        1
#define RM_TABLE        2

#define INDEX_L_BIT     (1 << 2)
#define INDEX_W_BIT     (1 << 3)
#define INDEX_B_BIT     (1 << 4)
#define INDEX_U_BIT     (1 << 5)
#define INDEX_P_BIT     (1 << 6)

extern const void* memu_armLoadStoreShortRmTable_0[16];
extern const void* memu_armLoadStoreShortRmTable_1[16];
extern const void* memu_armLoadStoreShortImmTable_0[16];
extern const void* memu_armLoadStoreShortImmTable_1[16];
extern const void* memu_armLoadStoreRmTable_0[16];
extern const void* memu_armLoadStoreRmTable_1[16];
extern const void* memu_armLoadStoreImmTable_0[16];
extern const void* memu_armLoadStoreImmTable_1[16];
extern const void* memu_armLdmStmRmTable_00[16];
extern const void* memu_armLdmStmRmTable_01[16];
extern const void* memu_armLdmStmRmTable_10[16];
extern const void* memu_armLdmStmRmTable_11[16];
extern const void* memu_armLoadRnTable_00[16];
extern const void* memu_armLoadRnTable_01[16];
extern const void* memu_armLoadRnTable_10[16];
extern const void* memu_armLoadRnTable_11[16];
extern const void* memu_armStoreRnTable_00[16];
extern const void* memu_armStoreRnTable_01[16];
extern const void* memu_armStoreRnTable_10[16];
extern const void* memu_armStoreRnTable_11[16];
extern const void* memu_armSwpRnTable[16];
extern const void* memu_armLdmStmRnTable_0[16];
extern const void* memu_armLdmStmRnTable_1[16];
extern const void* memu_armSwpbRdTable[16];
extern const void* memu_armSwpRdTable[16];
extern const void* memu_armLdrhRdTable[16];
extern const void* memu_armStrhRdTable[16];
extern const void* memu_armLdrsbRdTable[16];
extern const void* memu_armLdrshRdTable[16];
extern const void* memu_armLdrRdTable[16];
extern const void* memu_armStrRdTable[16];
extern const void* memu_armLdrbRdTable[16];
extern const void* memu_armStrbRdTable[16];
extern const void* memu_armLdmRdTable[16];
extern const void* memu_armStmRdTable[16];

// Ids of the handler tables. Tables that are selected by the same instruction
// bits are kept contiguous, so the variant bits can be added to the id of the
// first table of a group instead of branching to each table.
enum
{
    // halfword/signed transfers: (I << 1) | U, I set selects the immediate tables
    HANDLER_LOAD_STORE_SHORT_RM_0,
    HANDLER_LOAD_STORE_SHORT_RM_1,
    HANDLER_LOAD_STORE_SHORT_IMM_0,
    HANDLER_LOAD_STORE_SHORT_IMM_1,
    // word/byte transfers: (I << 1) | U, I set selects the Rm tables
    HANDLER_LOAD_STORE_IMM_0,
    HANDLER_LOAD_STORE_IMM_1,
    HANDLER_LOAD_STORE_RM_0,
    HANDLER_LOAD_STORE_RM_1,
    // Rn of the transfers: (P << 1) | W
    HANDLER_LOAD_RN_00,
    HANDLER_LOAD_RN_01,
    HANDLER_LOAD_RN_10,
    HANDLER_LOAD_RN_11,
    HANDLER_STORE_RN_00,
    HANDLER_STORE_RN_01,
    HANDLER_STORE_RN_10,
    HANDLER_STORE_RN_11,
    // Rm of ldm/stm: (P << 1) | U
    HANDLER_LDMSTM_RM_00,
    HANDLER_LDMSTM_RM_01,
    HANDLER_LDMSTM_RM_10,
    HANDLER_LDMSTM_RM_11,
    // Rd of ldr/str/ldrb/strb: (B << 1) | !L
    HANDLER_LDR_RD,
    HANDLER_STR_RD,
    HANDLER_LDRB_RD,
    HANDLER_STRB_RD,
    // remaining Rd/Rn tables
    HANDLER_LDRH_RD,
    HANDLER_STRH_RD,
    HANDLER_LDRSB_RD,
    HANDLER_LDRSH_RD,
    HANDLER_LDMSTM_RN_0,
    HANDLER_LDMSTM_RN_1,
    HANDLER_LDM_RD,
    HANDLER_STM_RD,
    HANDLER_SWP_RN,
    HANDLER_SWP_RD,
    HANDLER_SWPB_RD,
    HANDLER_COUNT
};

static const void* const handlerTables[HANDLER_COUNT] =
{
    memu_armLoadStoreShortRmTable_0,
    memu_armLoadStoreShortRmTable_1,
    memu_armLoadStoreShortImmTable_0,
    memu_armLoadStoreShortImmTable_1,
    memu_armLoadStoreImmTable_0,
    memu_armLoadStoreImmTable_1,
    memu_armLoadStoreRmTable_0,
    memu_armLoadStoreRmTable_1,
    memu_armLoadRnTable_00,
    memu_armLoadRnTable_01,
    memu_armLoadRnTable_10,
    memu_armLoadRnTable_11,
    memu_armStoreRnTable_00,
    memu_armStoreRnTable_01,
    memu_armStoreRnTable_10,
    memu_armStoreRnTable_11,
    memu_armLdmStmRmTable_00,
    memu_armLdmStmRmTable_01,
    memu_armLdmStmRmTable_10,
    memu_armLdmStmRmTable_11,
    memu_armLdrRdTable,
    memu_armStrRdTable,
    memu_armLdrbRdTable,
    memu_armStrbRdTable,
    memu_armLdrhRdTable,
    memu_armStrhRdTable,
    memu_armLdrsbRdTable,
    memu_armLdrshRdTable,
    memu_armLdmStmRnTable_0,
    memu_armLdmStmRnTable_1,
    memu_armLdmRdTable,
    memu_armStmRdTable,
    memu_armSwpRnTable,
    memu_armSwpRdTable,
    memu_armSwpbRdTable,
};

DTCM_DATA u8 memu_armDispatchTable[3][0x280];

/// @brief Encoded relative offset of each handler table, indexed by its handler id.
static u8 handlerTableOffsets[HANDLER_COUNT];

static bool isLoad(u32 index)
{
    return index & INDEX_L_BIT;
}

static bool isWriteback(u32 index)
{
    return index & INDEX_W_BIT;
}

static void setTableEntry(u32 table, u32 index, u32 handlerId)
{
    memu_armDispatchTable[table][index] = handlerTableOffsets[handlerId];
}

// (I << 1) | U for the halfword/signed transfers, I is instruction bit 22
static u32 getShortVariant(u32 index)
{
    return ((index >> 3) & 2) | ((index >> 5) & 1);
}

// (I << 1) | U for the word/byte transfers, I is instruction bit 25
static u32 getLoadStoreVariant(u32 index)
{
    return ((index >> 6) & 2) | ((index >> 5) & 1);
}

// (P << 1) | W selecting the writeback variant of the Rn table
static u32 getWritebackVariant(u32 index)
{
    return ((index >> 5) & 2) | ((index >> 3) & 1);
}

// (P << 1) | U selecting the variant of the ldm/stm Rm table
static u32 getLdmStmVariant(u32 index)
{
    return ((index >> 5) & 2) | ((index >> 5) & 1);
}

static void setSwpSwpbTableEntries(u32 index)
{
    setTableEntry(RM_TABLE, index, HANDLER_LOAD_STORE_SHORT_RM_1);
    setTableEntry(RN_TABLE, index, HANDLER_SWP_RN);
    // B
    setTableEntry(RD_TABLE, index, (index & INDEX_B_BIT) ? HANDLER_SWPB_RD : HANDLER_SWP_RD);
}

static void setLoadStoreShortRmEntry(u32 index)
{
    setTableEntry(RM_TABLE, index, HANDLER_LOAD_STORE_SHORT_RM_0 + getShortVariant(index));
}

static void setLoadStoreRnEntry(u32 index)
{
    setTableEntry(RN_TABLE, index,
        (isLoad(index) ? HANDLER_LOAD_RN_00 : HANDLER_STORE_RN_00) + getWritebackVariant(index));
}

static void setLdrhStrhTableEntries(u32 index)
{
    setLoadStoreShortRmEntry(index);
    setLoadStoreRnEntry(index);
    setTableEntry(RD_TABLE, index, isLoad(index) ? HANDLER_LDRH_RD : HANDLER_STRH_RD);
}

static void setLdrsbTableEntries(u32 index)
{
    setLoadStoreShortRmEntry(index);
    setLoadStoreRnEntry(index);
    setTableEntry(RD_TABLE, index, HANDLER_LDRSB_RD);
}

static void setLdrshTableEntries(u32 index)
{
    setLoadStoreShortRmEntry(index);
    setLoadStoreRnEntry(index);
    setTableEntry(RD_TABLE, index, HANDLER_LDRSH_RD);
}

static void setLdrLdrbStrStrbTableEntries(u32 index)
{
    setTableEntry(RM_TABLE, index, HANDLER_LOAD_STORE_IMM_0 + getLoadStoreVariant(index));
    setLoadStoreRnEntry(index);
    // (B << 1) | !L
    setTableEntry(RD_TABLE, index, HANDLER_LDR_RD
        + ((index >> 3) & 2)
        + (isLoad(index) ? 0 : 1));
}

static void setLdmStmTableEntries(u32 index)
{
    setTableEntry(RM_TABLE, index, HANDLER_LDMSTM_RM_00 + getLdmStmVariant(index));
    setTableEntry(RN_TABLE, index, isWriteback(index) ? HANDLER_LDMSTM_RN_1 : HANDLER_LDMSTM_RN_0);
    setTableEntry(RD_TABLE, index, isLoad(index) ? HANDLER_LDM_RD : HANDLER_STM_RD);
}

/// @brief Fills the three dispatch tables with the encoded entry point offsets.
///        The encoding is computed once per handler table instead of per entry.
void memu_initializeArmDispatchTable()
{
    for (u32 i = 0; i < HANDLER_COUNT; ++i)
    {
        handlerTableOffsets[i] = -(int)handlerTables[i] >> 6;
    }

    for (u32 index = 0; index < 0x280; ++index)
    {
        u32 type = index >> 8;
        switch (type)
        {
            case 0:
            {
                u32 subType = index & 3;
                switch (subType)
                {
                    case 0:
                    {
                        if ((index & INDEX_P_BIT) && !(index & INDEX_U_BIT) && !isWriteback(index) && !isLoad(index))
                        {
                            setSwpSwpbTableEntries(index);
                        }
                        break;
                    }
                    case 1:
                    {
                        setLdrhStrhTableEntries(index);
                        break;
                    }
                    case 2:
                    {
                        if (isLoad(index))
                        {
                            setLdrsbTableEntries(index);
                        }
                        break;
                    }
                    case 3:
                    {
                        if (isLoad(index))
                        {
                            setLdrshTableEntries(index);
                        }
                        break;
                    }
                }
                break;
            }
            case 1:
            {
                setLdrLdrbStrStrbTableEntries(index);
                break;
            }
            case 2:
            {
                setLdmStmTableEntries(index);
                break;
            }
        }
    }
}
