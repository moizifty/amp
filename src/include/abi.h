#ifndef ABI_H
#define ABI_H

#include "types.h"

typedef struct ArgABIInfo ArgABIInfo;
typedef enum ABIKind ABIKind;

enum ABIKind
{
    ABI_IGNORE,
	ABI_DIRECT_COERCE,
	ABI_DIRECT,
	ABI_INDIRECT,
};

struct ArgABIInfo
{
    ABIKind kind;
    //if the return type abi is indirect, then this dtermines the function parameters offset
    size_t startIndex;
    struct
    {
        bool zeroext : 1;
        bool signext : 1;
        bool byReg : 1;
        bool byVal : 1;
        bool realign : 1;
    }attr;

    union
    {
		struct
		{
			CheckerType *type;
		} directCoerce;
        struct
        {
			CheckerType *type;
        }direct;
        
		struct
		{
			CheckerType *type;
			int64_t alignment;
		} indirect;
    };
};

ArgABIInfo *allocArgABI(ABIKind kind);
ArgABIInfo *newArgABIIndirect(CheckerType *type);
ArgABIInfo *newArgABIDirectCoerce(CheckerType *type);
ArgABIInfo *newArgABIDirect(CheckerType *type);

ArgABIInfo *classifyCheckerTypeWin64(CheckerType *type);
ArgABIInfo *classifyCheckerType(CheckerType *type);
#endif