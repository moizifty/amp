#include "abi.h"
#include "compiler_internal.h"
#include "platform.h"

ArgABIInfo *allocArgABI(ABIKind kind)
{
    ArgABIInfo *a = malloc(sizeof(ArgABIInfo));
    a->kind = kind;
    a->startIndex = 0;

    a->attr.byReg = false;
    a->attr.byVal = false;
    a->attr.realign = false;
    a->attr.signext = false;
    a->attr.zeroext = false;
    return a;
}
ArgABIInfo *newArgABIIgnore()
{
    ArgABIInfo *a = allocArgABI(ABI_IGNORE);

    return a;
}
ArgABIInfo *newArgABIIndirect(CheckerType *type)
{
    ArgABIInfo *a = allocArgABI(ABI_INDIRECT);
    a->indirect.alignment = checkerTypeGetAlignment(type);
    a->indirect.type = type;

    return a;
}
ArgABIInfo *newArgABIDirectCoerce(CheckerType *type)
{
    ArgABIInfo *a = allocArgABI(ABI_DIRECT_COERCE);
    a->directCoerce.type = type;

    return a;
}
ArgABIInfo *newArgABIDirect(CheckerType *type)
{
    ArgABIInfo *a = allocArgABI(ABI_DIRECT);
    a->direct.type = type;

    return a;
}

ArgABIInfo *classifyCheckerTypeWin64(CheckerType *type)
{
    if(isTypeVoid(type)) return newArgABIIgnore();

    int64_t typeSize = checkerTypeGetSizeWin(type);

    if(isTypeAggregate(type))
    {
        // https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170
        bool isTypeSizePowerOf2 = (typeSize == 1) || (typeSize == 2) || (typeSize == 4) || (typeSize == 8);

        if(!isTypeSizePowerOf2 || (typeSize > 8))
        {
            return newArgABIIndirect(type);
        }

        return newArgABIDirectCoerce(type);
    }

    if(isTypeBoolean(type))
    {
        ArgABIInfo *ret = newArgABIDirect(type);
        ret->attr.zeroext = true;
        return ret;
    }

    if(typeSize > 8)
    {
        return newArgABIIndirect(type);
    }

    return newArgABIDirect(type);
}

ArgABIInfo *classifyCheckerType(CheckerType *type)
{
#if PLATFORM == PLATFORM_WIN64
    return classifyCheckerTypeWin64(type);
#endif
}
