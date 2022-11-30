#include "codegen-llvm.h"
#include "compiler_internal.h"
#include "abi.h"
#include <assert.h>

//must be the same as the runtime module naamespace
#define RUNTIME_NS_PREFIX   "__runtime"

LLVMContextRef context;
LLVMModuleRef module;
LLVMTargetMachineRef machine;
LLVMTargetDataRef targetDataRef;
LLVMBuilderRef builder;
LLVMDIBuilderRef debugBuilder;
LLVMPassManagerRef fnPassManager;
LLVMPassManagerRef modPassManager;
LLVMValueRef currAllocPoint;
bool generatingAssignLHS = false;

LLVMValueRef runtimeArraySetFunc;
LLVMValueRef runtimeArrayGetFunc;

extern GlobalContext globalContext;

void cgLLVMInitialValuesForProgGlobalVars(ASTDeclLL *globalVarConstLL)
{
    if(globalVarConstLL == NULL) return;

    ASTDeclLL *currDecl = globalVarConstLL->first;
    while(currDecl != NULL)
    {
        ASTDecl *d = currDecl->item;

        switch(d->kind)
        {
            case A_DECL_VAR:
            {  
                if(d->var.globalVarInitialCodeGenStmts != NULL)
                {
                    ASTStmtLL *currStmt = d->var.globalVarInitialCodeGenStmts->first;
                    while(currStmt != NULL)
                    {
                        if(currStmt->item != NULL) cgLLVMStmt(currStmt->item);
                        currStmt = currStmt->next;
                    }
                }
                if(d->var.initial != NULL)
                {
                    ASTExpr *idenExpr = newASTExprIden(d->var.idenExpr->iden);
                    idenExpr->checkType = d->var.type->checkType;
                    idenExpr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                    idenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                    idenExpr->idenSymEntry = _symTableLookupGlobal(d->tbl, d->var.idenExpr->iden.lexeme);
                    
                    ASTStmt *assignStmt = newASTStmtAssign(idenExpr, d->var.initial);

                    cgLLVMStmt(assignStmt);
                }
            }break;
            case A_DECL_CONST:
            {
                if(d->constDecl.initial != NULL)
                {
                    ASTExpr *idenExpr = newASTExprIden(d->constDecl.iden);
                    idenExpr->checkType = d->constDecl.type->checkType;
                    idenExpr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                    idenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                    idenExpr->idenSymEntry = _symTableLookupGlobal(d->tbl, d->constDecl.iden.lexeme);
                    
                    ASTStmt *assignStmt = newASTStmtAssign(idenExpr, d->constDecl.initial);

                    cgLLVMStmt(assignStmt);
                }
            }break;
        }

        currDecl = currDecl->next;
    }
}

void cgLLVMEntireProgram(TypeTable *typeTable, NamespaceTable *namespaceTable)
{
    initLLVMCodeGen();

    //emit array view empty global
    {
        LLVMTypeRef typeRef = cgLLVMCheckerTypeToTypeRef(arrayViewType, false);
        LLVMValueRef emptyArrayView = LLVMAddGlobal(module,  typeRef, "ArrayViewEmpty");

        LLVMSetGlobalConstant(emptyArrayView, true);
        LLVMSetAlignment(emptyArrayView, LLVMABIAlignmentOfType(targetDataRef, typeRef));

        LLVMSetInitializer(emptyArrayView, LLVMConstNull(typeRef));
    }
    size_t numTypes = globalContext.typeTable->typeLL->first->numItems;

    { //check all types before emitting type tables
        cgLLVMNamespaceTableDoAllTypes(namespaceTable);
    }
    
    { //generate valuerefs for decls so toString functions can b  found
        {// generate value refs for decls
            NSTEntryLL *currNSEntryLL = namespaceTable->entries->first;
            while(currNSEntryLL != NULL)
            {
                if(currNSEntryLL->item->isImported)
                {
                    ASTProgLL *currProgLL = currNSEntryLL->item->progASTs;
                    if(currProgLL != NULL) currProgLL = currProgLL->first;
                    while(currProgLL != NULL)
                    {
                        if(!currProgLL->item->hasGeneratedDecls) cgLLVMProgsDeclValueRefs(currProgLL->item);
                        currProgLL = currProgLL->next;
                    }
                }
                currNSEntryLL = currNSEntryLL->next;
            }
        }
    }

    //emit namespace table data global
    {
        size_t numNamespaces = namespaceTable->entries->first->numItems;
        LLVMTypeRef nsInfoTypeRef = cgLLVMCheckerTypeToTypeRef(namespaceInfoType, false);
        LLVMTypeRef nsTableGlobalType = LLVMArrayType(nsInfoTypeRef, numNamespaces);
        LLVMValueRef nsTableGlobal = LLVMAddGlobal(module, nsTableGlobalType, "__namespaceTable");

        LLVMSetAlignment(nsTableGlobal, LLVMABIAlignmentOfType(targetDataRef, nsTableGlobalType));

        LLVMValueRef *initializerElems = malloc(sizeof(LLVMValueRef) * numNamespaces);

        NSTEntryLL *currNSLL = namespaceTable->entries->first;

        for(size_t i = 0; i < numNamespaces; i++)
        {
            initializerElems[i] = cgLLVMNSTableEntry(currNSLL->item);
            currNSLL->item->indexInNamespaceTable = i;
            currNSLL = currNSLL->next;
        }

        LLVMValueRef initializer = LLVMConstArray(nsInfoTypeRef, initializerElems, numNamespaces);

        LLVMSetInitializer(nsTableGlobal, initializer);
    }

    //emit typetable global
    {

        LLVMTypeRef typeInfoTypeRef = cgLLVMCheckerTypeToTypeRef(typeInfoType, false);
        LLVMTypeRef typeTableGlobalType = LLVMArrayType(typeInfoTypeRef, numTypes);
        LLVMValueRef typeTableGlobal = LLVMAddGlobal(module, typeTableGlobalType, "__typeTable");

        LLVMSetAlignment(typeTableGlobal, LLVMABIAlignmentOfType(targetDataRef, typeTableGlobalType));

        LLVMValueRef *initializerElems = malloc(sizeof(LLVMValueRef) * numTypes);

        CheckerTypeLL *currTypeLL = globalContext.typeTable->typeLL->first;

        for(size_t i = 0; i < numTypes; i++)
        {
            CheckerType *currType = currTypeLL->item;
            bool isUsed = true;
            if(currType->namespaceName != NULL)
            {
                NSTEntry *e = namespaceTableLookupEntry(namespaceTable, currType->namespaceName);
                isUsed = e->isImported;
            }

            if(isUsed)
            {
                initializerElems[i] = cgLLVMTypeTableEntry(currType);
            }
            else initializerElems[i] = LLVMConstNull(typeInfoTypeRef);

            currTypeLL = currTypeLL->next;
        }

        LLVMValueRef initializer = LLVMConstArray(typeInfoTypeRef, initializerElems, numTypes);

        LLVMSetInitializer(typeTableGlobal, initializer);
    }

    cgLLVMNamespaceTable(namespaceTable);

    currAllocPoint = NULL;

    //     { //emit global value initializers
    //         NSTEntryLL *currNSEntryLL = namespaceTable->entries->first;
    //         while(currNSEntryLL != NULL)
    //         {
    //             ASTProgLL *currProgLL = currNSEntryLL->item->progASTs;
    //             if(currProgLL != NULL) currProgLL = currProgLL->first;
    //             while(currProgLL != NULL)
    //             {
    //                 cgLLVMInitialValuesForProgGlobalVars(currProgLL->item->globalVarOrConstDecls);
    //                 currProgLL = currProgLL->next;
    //             }

    //             currNSEntryLL = currNSEntryLL->next;
    //         }
    //     }

    //LLVMRunPassManager(modPassManager, module);

    char *outputFilenameWithoutExt = globalContext.gc.outputName;

    char outName[MAX_PATH];
    sprintf(outName, "%s.ll", outputFilenameWithoutExt);
    
    LLVMDIBuilderFinalize(debugBuilder);
    char *printModuleError = NULL;
    char *printModuleVerify = NULL;

    LLVMPrintModuleToFile(module, outName, &printModuleError);
    LLVMVerifyModule(module, LLVMPrintMessageAction, &printModuleVerify);

    char *error = NULL;
    sprintf(outName, "%s.obj", outputFilenameWithoutExt);
    LLVMTargetMachineEmitToFile(machine, module, outName, LLVMObjectFile, &error);

    if(error != NULL)
    {
        fprintf(stderr, "Error generating obj file: %s\n", error);
    }
}

void initLLVMCodeGen()
{
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();

    context = LLVMContextCreate();
    module = LLVMModuleCreateWithNameInContext("Amp module", context);
    
    fnPassManager = LLVMCreateFunctionPassManagerForModule(module);
    LLVMAddInstructionCombiningPass(fnPassManager);
    LLVMAddReassociatePass(fnPassManager);
    LLVMAddGVNPass(fnPassManager);
    LLVMAddCFGSimplificationPass(fnPassManager);
    
    modPassManager = LLVMCreatePassManager();
    LLVMAddConstantMergePass(modPassManager);

    LLVMInitializeFunctionPassManager(fnPassManager);

    builder = LLVMCreateBuilderInContext(context);
    debugBuilder = LLVMCreateDIBuilder(module);
    LLVMDIBuilderCreateCompileUnit(debugBuilder, LLVMDWARFSourceLanguageC, 
                                   LLVMDIBuilderCreateFile(debugBuilder, "demo2.amp", 9, "tests\\", 5), 
                                   "ampc", 4, false, "", 0, 1, "", 0, LLVMDWARFEmissionFull, 0, FALSE, FALSE, "", 0, ""
, 0);

    char * tt = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, tt);

    char *err = NULL;
    LLVMTargetRef target = NULL;
    LLVMGetTargetFromTriple(tt, &target, &err);

    char *cpu = LLVMGetHostCPUName();
    char *cpuFeatures = LLVMGetHostCPUFeatures();

    machine = LLVMCreateTargetMachine(target, tt, cpu, cpuFeatures, LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);

    targetDataRef = LLVMCreateTargetDataLayout(machine);

    LLVMSetModuleDataLayout(module, targetDataRef);
}

void cgLLVMNamespaceTableDoAllTypes(NamespaceTable *nsTable)
{
    { //type decls
        NSTEntryLL *currNSEntryLL = nsTable->entries->first;
        while(currNSEntryLL != NULL)
        {
            if(currNSEntryLL->item->isImported)
            {
                ASTProgLL *currProgLL = currNSEntryLL->item->progASTs;
                if(currProgLL != NULL) currProgLL = currProgLL->first;
                while(currProgLL != NULL)
                {
                    if(!currProgLL->item->hasGeneratedHeader) cgLLVMTypesDecls(currProgLL->item);
                    currProgLL = currProgLL->next;
                }
            }

            currNSEntryLL = currNSEntryLL->next;
        }
    }
  
}
void cgLLVMNamespaceTable(NamespaceTable *nsTable)
{
    runtimeArrayGetFunc = LLVMGetNamedFunction(module, RUNTIME_NS_PREFIX "_arrayGet");
    runtimeArraySetFunc = LLVMGetNamedFunction(module, RUNTIME_NS_PREFIX "_arraySet");

    //generate program decls
    {
        NSTEntryLL *currNSEntryLL = nsTable->entries->first;
        while(currNSEntryLL != NULL)
        {
            if(currNSEntryLL->item->isImported)
            {
                ASTProgLL *currProgLL = currNSEntryLL->item->progASTs;
                if(currProgLL != NULL) currProgLL = currProgLL->first;
                while(currProgLL != NULL)
                {
                    if(!currProgLL->item->hasGeneratedDecls) cgLLVMProg(currProgLL->item);
                    currProgLL = currProgLL->next;
                }
            }
            currNSEntryLL = currNSEntryLL->next;
        }
    } 
}

LLVMValueRef cgLLVMNSTableEntry(NSTEntry *entry)
{
    LLVMTypeRef nsInfoTypeRef = cgLLVMCheckerTypeToTypeRef(namespaceInfoType, false);
    char *nsName = allocNamespaceNameWithoutUnderscores(entry);

    size_t numFiles = (entry->progASTs != NULL) ? entry->progASTs->first->numItems : 0;

    LLVMValueRef nsNameStr = cgLLVMCreateStringConstantAndGlobal(nsName, strlen(nsName), false);
    LLVMValueRef filesArray = LLVMAddGlobal(module, LLVMArrayType(cgLLVMCheckerTypeToTypeRef(stringType, false), numFiles), "");
    
    LLVMValueRef filesArrayView;

    if(numFiles != 0)
    {
        { //create enum memb array

            LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numFiles);

            ASTProgLL *currProgLL = entry->progASTs->first;
            size_t index = 0;
            while(currProgLL != NULL)
            {
                LLVMValueRef strData = cgLLVMCreateStringConstantAndGlobal(currProgLL->item->filename, strlen(currProgLL->item->filename), false);

                initElems[index] = strData;
                
                index++;
                currProgLL = currProgLL->next;
            }

            LLVMSetInitializer(filesArray, LLVMConstArray(cgLLVMCheckerTypeToTypeRef(stringType, false), initElems, numFiles));
        }

        { //create array view

            LLVMValueRef arrViewInit[] =
            {
                LLVMConstInt(LLVMInt64TypeInContext(context), numFiles, false),
                LLVMConstBitCast(filesArray, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, cgLLVMCheckerTypeToTypeRef(stringType, false)), false),
            };

            filesArrayView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
        }
    }
    else filesArrayView = LLVMConstNull(cgLLVMCheckerTypeToTypeRef(arrayViewType, false));

    LLVMValueRef membs[2] = 
    {
        nsNameStr,
        filesArrayView,
    };

    return LLVMConstNamedStruct(nsInfoTypeRef, membs, 2);
}
LLVMValueRef cgLLVMTypeTableEntry(CheckerType *type)
{
    typedef enum TypeKind
    {
        TYPE_INT =          0,
        TYPE_FLOAT =        1,
        TYPE_STRING =       2,
        TYPE_BOOL =         3,
        TYPE_ARRAY =        4,
        TYPE_ENUM =         5,
        TYPE_STRUCT =       6,
        TYPE_UNION =        7,
        TYPE_TAGGED_UNION = 8,
        TYPE_POINTER =      9,
        TYPE_FUNC =         10,
        TYPE_OPERFUNC =     11,
        TYPE_VOID =         12,
        TYPE_NAMESPACE =    13,
        TYPE_ALIASED =      14,
        TYPE_TUPLE =        15,
    }TypeKind;

    LLVMTypeRef typeInfoTypeRef = cgLLVMCheckerTypeToTypeRef(typeInfoType, false);
    LLVMValueRef typeNameVal = NULL;
    size_t sizeOfType = checkerTypeGetSize(type);
    TypeKind typeKind = 0;
    
    { //type kind
        switch(type->kind)
        {
            case C_TYPE_INT: typeKind = TYPE_INT; break;
            case C_TYPE_FLOAT: typeKind = TYPE_FLOAT; break;
            case C_TYPE_BOOL: typeKind = TYPE_BOOL; break;
            case C_TYPE_STRUCT: 
            {
                if(areTypesEqual(type, stringType))  typeKind = TYPE_STRING;
                else typeKind = TYPE_STRUCT;
            }break;
            case C_TYPE_TUPLE: typeKind = TYPE_TUPLE; break;
            case C_TYPE_ARRAY: typeKind = TYPE_ARRAY; break;
            case C_TYPE_ENUM: typeKind = TYPE_ENUM; break;
            case C_TYPE_UNION: typeKind = TYPE_UNION; break;
            case C_TYPE_TAGGED_UNION: typeKind = TYPE_TAGGED_UNION; break;
            case C_TYPE_POINTER: typeKind = TYPE_POINTER; break;
            case C_TYPE_FUNC: typeKind = TYPE_FUNC; break;
            case C_TYPE_VOID: typeKind = TYPE_VOID; break;
            case C_TYPE_NAMESPACE: typeKind = TYPE_NAMESPACE; break;
            case C_TYPE_ALIASED: typeKind = TYPE_ALIASED; break;
        }
    }

    { //type name
        char *typeName = allocCheckerTypeToString(type);
        LLVMValueRef typeNameGlobal = cgLLVMCreateCStringConstantFromHashmap(globalContext.gc.stringHashMap, typeName, strlen(typeName), false);

        LLVMValueRef stringMembs[2] =
        {
            LLVMConstBitCast(typeNameGlobal, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
            LLVMConstInt(LLVMInt64TypeInContext(context), strlen(typeName), false),
        };

        typeNameVal = LLVMConstNamedStruct(LLVMGetTypeByName2(context, "prelude_string"), stringMembs, 2);
    }

    LLVMValueRef typeUnionVal = cgLLVMTypeTableEntryTypeUnion(type);
    LLVMValueRef toStringFuncPtr = LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(context), 0));

    { //find toString method
        SymEntry *funcEntry = typeMethodTableLookUp(globalContext.typeMethodTable, type, "toString");

        if((funcEntry != NULL) && (CHECK_TYPE_FLAG(funcEntry->type, TYPE_TOSTRING_FUNC_FLAG)))
        {
            char buf[MAX_IDEN_LEN * 3] = {0};
            strcpy(buf, allocCheckerTypeName(type));
            strcat(buf, "_");
            strcat(buf, funcEntry->name);

            LLVMValueRef fn = LLVMGetNamedFunction(module, buf);
            //global_s_s1_toString;
            LLVMValueRef fnBtcst = LLVMConstBitCast(fn, LLVMPointerType(LLVMInt8TypeInContext(context), 0));

            toStringFuncPtr = fnBtcst;
        }
    }

    LLVMValueRef m[5] = 
    {
        LLVMConstInt(LLVMInt32TypeInContext(context), typeKind, false),
        typeUnionVal,
        LLVMConstInt(LLVMInt64TypeInContext(context), sizeOfType, false),
        typeNameVal,
        toStringFuncPtr,
    };

    return LLVMConstNamedStruct(typeInfoTypeRef, m, 5);
}
LLVMValueRef cgLLVMTypeTableEntryTypeUnion(CheckerType *type)
{
    LLVMTypeRef typeInfoTypeRef = cgLLVMCheckerTypeToTypeRef(typeInfoType, false);
    LLVMTypeRef tuTR = LLVMGetTypeByName2(context, "prelude_TypeUnion");
    LLVMTypeRef tuTRBiggestMemb = NULL;
    LLVMGetStructElementTypes(tuTR, &tuTRBiggestMemb);

    LLVMValueRef lit = LLVMConstNull(tuTR);

    switch(type->kind)
    {
        case C_TYPE_ARRAY:
        {
            LLVMTypeRef tuArrayTR = LLVMGetTypeByName2(context, "prelude_TypeInfoArray");
            LLVMValueRef arrTULit = LLVMAddGlobal(module, tuArrayTR, "");
            LLVMSetGlobalConstant(arrTULit, true);

            bool isSlice = isTypeSliceArray(type);
            LLVMValueRef baseTypeInfo = cgLLVMTypesTypeEntry(type->arrayType.base);
            size_t arrLen = (isSlice) ? 0 : type->arrayType.size;

            LLVMValueRef membs[] =
            {
                LLVMConstInt(LLVMInt1TypeInContext(context), isSlice, false),
                baseTypeInfo,
                LLVMConstInt(LLVMInt64TypeInContext(context), arrLen, false),
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuArrayTR, membs, 3);

            LLVMSetInitializer(arrTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(arrTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_ENUM:
        {
            LLVMTypeRef tuEnumTR = LLVMGetTypeByName2(context, "prelude_TypeInfoEnum");
            LLVMTypeRef tuEnumMembTR = LLVMGetTypeByName2(context, "prelude_TypeInfoEnumMemb");
            LLVMValueRef enumTULit = LLVMAddGlobal(module, tuEnumTR, "");
            LLVMSetGlobalConstant(enumTULit, true);

            size_t numMembs = type->enumType.membLL->first->numItems;

            LLVMTypeRef eMembsType = LLVMArrayType(tuEnumMembTR, numMembs);
            LLVMValueRef eMembs = LLVMAddGlobal(module, eMembsType, "");
            int64_t max = INT32_MIN;
            int64_t min = INT32_MAX;
            
            { //create enum memb array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numMembs);

                EnumMembLL *currMembLL = type->enumType.membLL->first;
                size_t index = 0;
                while(currMembLL != NULL)
                {
                    LLVMValueRef membVal = NULL;

                    LLVMValueRef stctInit[] =
                    {
                        cgLLVMCreateStringConstantAndGlobal(currMembLL->item->name, strlen(currMembLL->item->name), true),
                        cgLLVMTypesTypeEntry(type),
                        LLVMConstInt(cgLLVMCheckerTypeToTypeRef(intType, false), currMembLL->item->val, false),
                    };

                    membVal = LLVMConstNamedStruct(tuEnumMembTR, stctInit, 3);

                    if(currMembLL->item->val > max) max = currMembLL->item->val;
                    if(currMembLL->item->val < min) min = currMembLL->item->val;

                    initElems[index] = membVal;
                    
                    index++;
                    currMembLL = currMembLL->next;
                }

                LLVMSetInitializer(eMembs, LLVMConstArray(tuEnumMembTR, initElems, numMembs));
            }

            LLVMValueRef eMembsArrView = NULL;
            { //create array view

                LLVMValueRef arrViewInit[] =
                {
                    LLVMConstInt(LLVMInt64TypeInContext(context), numMembs, false),
                    LLVMConstBitCast(eMembs, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, tuEnumMembTR), false),
                };

                eMembsArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
            }
            
            bool isFlag = (CHECK_TYPE_FLAG(type, TYPE_ENUM_FLAGS));


            LLVMValueRef membs[] =
            {
                LLVMConstNull(cgLLVMCheckerTypeToTypeRef(stringType, false)),
                eMembsArrView,
                LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false), max, false),
                LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false), min, false),
                LLVMConstInt(cgLLVMCheckerTypeToTypeRef(boolType, false), isFlag, false),
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuEnumTR, membs, 5);

            LLVMSetInitializer(enumTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(enumTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_STRUCT:
        {
            LLVMTypeRef tuStructTR = LLVMGetTypeByName2(context, "prelude_TypeInfoStruct");
            LLVMTypeRef tuStructMembTR = LLVMGetTypeByName2(context, "prelude_TypeInfoStructMemb");
            LLVMValueRef structTULit = LLVMAddGlobal(module, tuStructTR, "");
            LLVMSetGlobalConstant(structTULit, true);

            size_t numMembs = type->structType.declLL->first->numItems;

            LLVMTypeRef sMembsType = LLVMArrayType(tuStructMembTR, numMembs);
            LLVMValueRef sMembs = LLVMAddGlobal(module, sMembsType, "");
            
            { //create enum memb array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numMembs);

                ScopedDeclLL *currMembLL = type->structType.declLL->first;
                size_t index = 0;
                while(currMembLL != NULL)
                {
                    LLVMValueRef membVal = NULL;
                    LLVMTypeRef structGeneratingForTypeRef = cgLLVMCheckerTypeToTypeRef(type, false);

                    LLVMValueRef stctInit[] =
                    {
                        cgLLVMCreateStringConstantAndGlobal(currMembLL->item->name.lexeme, strlen(currMembLL->item->name.lexeme), true),
                        cgLLVMTypesTypeEntry(currMembLL->item->type),
                        LLVMConstInt(LLVMInt64TypeInContext(context), LLVMOffsetOfElement(targetDataRef, structGeneratingForTypeRef, index) , false),
                    };

                    membVal = LLVMConstNamedStruct(tuStructMembTR, stctInit, 3);

                    initElems[index] = membVal;
                    
                    index++;
                    currMembLL = currMembLL->next;
                }

                LLVMSetInitializer(sMembs, LLVMConstArray(tuStructMembTR, initElems, numMembs));
            }

            LLVMValueRef sMembsArrView = NULL;
            { //create array view

                LLVMValueRef arrViewInit[] =
                {
                    LLVMConstInt(LLVMInt64TypeInContext(context), numMembs, false),
                    LLVMConstBitCast(sMembs, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, tuStructMembTR), false),
                };

                sMembsArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
            }

            LLVMValueRef membs[] =
            {
                LLVMConstNull(cgLLVMCheckerTypeToTypeRef(stringType, false)),
                sMembsArrView,
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuStructTR, membs, 2);

            LLVMSetInitializer(structTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(structTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_TAGGED_UNION:
        {
            LLVMTypeRef tuTGUR = LLVMGetTypeByName2(context, "prelude_TypeInfoTaggedUnion");
            LLVMTypeRef tuTGUMembTR = LLVMGetTypeByName2(context, "prelude_TypeInfoUnionMemb");
            LLVMValueRef structTULit = LLVMAddGlobal(module, tuTGUR, "");
            LLVMSetGlobalConstant(structTULit, true);

            size_t numMembs = type->taggedUnionType.declLL->first->numItems;

            LLVMTypeRef sMembsType = LLVMArrayType(tuTGUMembTR, numMembs);
            LLVMValueRef sMembs = LLVMAddGlobal(module, sMembsType, "");
            LLVMTypeRef typeGeneratingForTypeRef = cgLLVMCheckerTypeToTypeRef(type, false);
            
            { //create enum memb array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numMembs);

                ScopedDeclLL *currMembLL = type->taggedUnionType.declLL->first;
                size_t index = 0;
                while(currMembLL != NULL)
                {
                    LLVMValueRef membVal = NULL;

                    LLVMValueRef stctInit[] =
                    {
                        cgLLVMCreateStringConstantAndGlobal(currMembLL->item->name.lexeme, strlen(currMembLL->item->name.lexeme), true),
                        cgLLVMTypesTypeEntry(currMembLL->item->type),
                    };

                    membVal = LLVMConstNamedStruct(tuTGUMembTR, stctInit, 2);

                    initElems[index] = membVal;
                    
                    index++;
                    currMembLL = currMembLL->next;
                }

                LLVMSetInitializer(sMembs, LLVMConstArray(tuTGUMembTR, initElems, numMembs));
            }

            LLVMValueRef sMembsArrView = NULL;
            { //create array view

                LLVMValueRef arrViewInit[] =
                {
                    LLVMConstInt(LLVMInt64TypeInContext(context), numMembs, false),
                    LLVMConstBitCast(sMembs, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, tuTGUMembTR), false),
                };

                sMembsArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
            }

            LLVMValueRef membs[] =
            {
                LLVMConstNull(cgLLVMCheckerTypeToTypeRef(stringType, false)),
                sMembsArrView,
                LLVMConstInt(LLVMInt64TypeInContext(context), LLVMOffsetOfElement(targetDataRef, typeGeneratingForTypeRef, 1), false), //offset of kind member
                LLVMConstInt(LLVMInt64TypeInContext(context), LLVMOffsetOfElement(targetDataRef, typeGeneratingForTypeRef, 3), false), //offset of kind member
                LLVMConstInt(LLVMInt64TypeInContext(context), LLVMOffsetOfElement(targetDataRef, typeGeneratingForTypeRef, 2), false), //offset of kind member
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuTGUR, membs, 5);

            LLVMSetInitializer(structTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(structTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_POINTER:
        {
            LLVMTypeRef tuPointerTR = LLVMGetTypeByName2(context, "prelude_TypeInfoPointer");
            LLVMValueRef ptrTULit = LLVMAddGlobal(module, tuPointerTR, "");
            LLVMSetGlobalConstant(ptrTULit, true);

            //todo add typealias name
            LLVMValueRef membs[] =
            {
                cgLLVMTypesTypeEntry(type->pointerType.base),
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuPointerTR, membs, 1);

            LLVMSetInitializer(ptrTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(ptrTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_ALIASED:
        {
            LLVMTypeRef tuAliasedTR = LLVMGetTypeByName2(context, "prelude_TypeInfoAliased");
            LLVMValueRef alTULit = LLVMAddGlobal(module, tuAliasedTR, "");
            LLVMSetGlobalConstant(alTULit, true);

            //todo add typealias name
            LLVMValueRef membs[] =
            {
                LLVMConstNull(cgLLVMCheckerTypeToTypeRef(stringType, false)),
                cgLLVMTypesTypeEntry(type->aliasedType.base),
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuAliasedTR, membs, 2);

            LLVMSetInitializer(alTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(alTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_TUPLE:
        {
            LLVMTypeRef tuTupleTR = LLVMGetTypeByName2(context, "prelude_TypeInfoTuple");
            LLVMValueRef tupleTULit = LLVMAddGlobal(module, tuTupleTR, "");
            LLVMSetGlobalConstant(tupleTULit, true);

            size_t numMembs = type->tupleType.numTypes;
            
            LLVMTypeRef tuInforArrType = LLVMArrayType(LLVMPointerType(typeInfoTypeRef, 0), numMembs);
            LLVMValueRef tuInfosArrGlobal = LLVMAddGlobal(module, tuInforArrType, "");

            { //create tuple type info array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numMembs);

                for(size_t i = 0; i < numMembs; i++)
                {
                    CheckerType *currType = type->tupleType.types[i];

                    initElems[i] = cgLLVMTypesTypeEntry(currType);
                }
                
                LLVMSetInitializer(tuInfosArrGlobal, LLVMConstArray(LLVMPointerType(typeInfoTypeRef, 0), initElems, numMembs));
            }

            LLVMTypeRef tuOffsetsArrType = LLVMArrayType(LLVMInt64TypeInContext(context), numMembs);
            LLVMValueRef tuOffsetssArrGlobal = LLVMAddGlobal(module, tuOffsetsArrType, "");
            { //create tuple type info array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numMembs);
                LLVMTypeRef tupleTypeBeingGenerated = cgLLVMCheckerTypeToTypeRef(type, false);
                for(size_t i = 0; i < numMembs; i++)
                {
                    initElems[i] = LLVMConstInt(LLVMInt64TypeInContext(context), LLVMOffsetOfElement(targetDataRef, tupleTypeBeingGenerated, i), false);
                }
                
                LLVMSetInitializer(tuOffsetssArrGlobal, LLVMConstArray(LLVMInt64TypeInContext(context), initElems, numMembs));
            }

            LLVMValueRef tuInfosArrView = NULL;
            LLVMValueRef tuOffsetsArrView = NULL;
            { //create array view for type info

                LLVMValueRef arrViewInit[] =
                {
                    LLVMConstInt(LLVMInt64TypeInContext(context), numMembs, false),
                    LLVMConstBitCast(tuInfosArrGlobal, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, LLVMPointerType(typeInfoTypeRef, 0)), false),
                };

                tuInfosArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
            }

            { //create array view for offsets

                LLVMValueRef arrViewInit[] =
                {
                    LLVMConstInt(LLVMInt64TypeInContext(context), numMembs, false),
                    LLVMConstBitCast(tuOffsetssArrGlobal, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, LLVMInt64TypeInContext(context)), false),
                };

                tuOffsetsArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
            }

            LLVMValueRef membs[] =
            {
                tuInfosArrView,
                tuOffsetsArrView,
            };

            LLVMValueRef constant = LLVMConstNamedStruct(tuTupleTR, membs, 2);

            LLVMSetInitializer(tupleTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(tupleTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;

        case C_TYPE_FUNC:
        {
            LLVMTypeRef fnTUTR = LLVMGetTypeByName2(context, "prelude_TypeInfoFunc");
            LLVMValueRef fnTULit = LLVMAddGlobal(module, fnTUTR, "");
            LLVMSetGlobalConstant(fnTULit, true);

            size_t numParams = (type->funcType.paramLL == NULL) ? 0 : type->funcType.paramLL->first->numItems;

            LLVMValueRef fnInfosArrView = NULL;

            { //create tuple type info array

                LLVMValueRef *initElems = malloc(sizeof(LLVMValueRef) * numParams);

                if(numParams != 0)
                {
                    LLVMTypeRef fnInforArrType = LLVMArrayType(LLVMPointerType(typeInfoTypeRef, 0), numParams);
                    LLVMValueRef fnInfosArrGlobal = LLVMAddGlobal(module, fnInforArrType, "");

                    ScopedDeclLL *currParamLL = type->funcType.paramLL->first;
                    size_t index = 0;
                    while(currParamLL != NULL)
                    {
                        initElems[index] = cgLLVMTypesTypeEntry(currParamLL->item->type);

                        index++;
                        currParamLL = currParamLL->next;
                    }
                    LLVMSetInitializer(fnInfosArrGlobal, LLVMConstArray(LLVMPointerType(typeInfoTypeRef, 0), initElems, numParams));
                    
                    { //create array view for type info

                        LLVMValueRef arrViewInit[] =
                        {
                            LLVMConstInt(LLVMInt64TypeInContext(context), numParams, false),
                            LLVMConstBitCast(fnInfosArrGlobal, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                            LLVMConstInt(LLVMInt64TypeInContext(context), LLVMABISizeOfType(targetDataRef, LLVMPointerType(typeInfoTypeRef, 0)), false),
                        };

                        fnInfosArrView = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(arrayViewType, false), arrViewInit, 3);
                    }
                }
                else fnInfosArrView = LLVMConstNull(cgLLVMCheckerTypeToTypeRef(arrayViewType, false));
            }


            LLVMValueRef membs[] =
            {
                fnInfosArrView,
                cgLLVMTypesTypeEntry(type->funcType.ret),
            };

            LLVMValueRef constant = LLVMConstNamedStruct(fnTUTR, membs, 2);

            LLVMSetInitializer(fnTULit, constant);

            LLVMValueRef btcst = LLVMConstBitCast(fnTULit, tuTRBiggestMemb);

            lit = LLVMConstNamedStruct(tuTR, &btcst, 1);
        }break;
    }

    return lit;
}

void cgLLVMTypesPrototypes(ASTProg *p)
{
    { //do breif definitions first
        {
            ASTDeclLL *currDeclLL = p->globalStructuredDecls;

            if(currDeclLL != NULL)
            {
                currDeclLL = currDeclLL->first;

                while(currDeclLL != NULL)
                {
                    //todo
                    ASTDecl *d = currDeclLL->item;

                    switch(d->kind)
                    {
                        case A_DECL_UNION_TAGGED:
                        {
                            char buf[MAX_IDEN_LEN * 2] = {0};
                            sprintf(buf, "%s_%s", d->declType->namespaceName, d->declType->taggedUnionType.name);
                            d->declType->backendType = LLVMStructCreateNamed(context, buf);
                        }break;
                        case A_DECL_UNION:
                        {
                            char buf[MAX_IDEN_LEN * 2] = {0};
                            sprintf(buf, "%s_%s", d->declType->namespaceName, d->declType->unionType.name);
                            d->declType->backendType = LLVMStructCreateNamed(context, buf);
                        }break;
                        case A_DECL_STRUCT:
                        {
                            char buf[MAX_IDEN_LEN * 2] = {0};
                            sprintf(buf, "%s_%s", d->declType->namespaceName, d->declType->structType.name);
                            d->declType->backendType = LLVMStructCreateNamed(context, buf);
                        }break;
                    }
                    currDeclLL = currDeclLL->next;
                }
            }
        }
    }
}
void cgLLVMTypesDecls(ASTProg *p)
{
    //todo tuples:
    {

    }
    
    //structs unions
    {
        //actual definitions
        {
            ASTDeclLL *currDeclLL = p->globalStructuredDecls;

            if(currDeclLL != NULL)
            {
                currDeclLL = currDeclLL->first;

                while(currDeclLL != NULL)
                {
                    //todo
                    ASTDecl *d = currDeclLL->item;

                    switch(d->kind)
                    {
                        case A_DECL_UNION_TAGGED:
                        case A_DECL_UNION:
                        case A_DECL_STRUCT:
                        {
                            cgLLVMStructUnionEmbedded(d->declType);
                        }break;
                    }

                    currDeclLL = currDeclLL->next;
                }
            }
        }
    }

    // { //tuples
    //     CheckerTypeLL *allTuplesTypes = globalContext.cc.tupleTypes;

    //     if(allTuplesTypes != NULL)
    //     {
    //         allTuplesTypes = allTuplesTypes->first;

    //         while(allTuplesTypes != NULL)
    //         {
    //             //todo
    //             CheckerType *t = allTuplesTypes->item;

    //             if(!CHECK_TYPE_FLAG(t, TYPE_FOREIGN_FLAG))
    //             {
    //                 cgStructUnionEmbedded(t);
    //             }
    //             allTuplesTypes = allTuplesTypes->next;
    //         }
    //     }
    // }
}
void cgLLVMStructUnionEmbedded(CheckerType *type)
{
    bool isUnion = isTypeUnion(type);
    bool isStruct = isTypeStruct(type);
    bool isTaggedUnion = isTypeTaggedUnion(type);
    bool isTuple = isTypeTuple(type);

    StructUnionDependencyNode *dependencyNode = (isStruct) ? type->structType.dependencies : ((isUnion) ? type->unionType.dependencies : ((isTaggedUnion) ? type->taggedUnionType.dependencies : type->tupleType.dependencies));

    { //emit all structs/union this struct depends on first

        StructUnionDependencyNodeLL *children = dependencyNode->children;
        while(children != NULL)
        {  
            cgLLVMStructUnionEmbedded(children->item->type);

            children = children->prev;
        }

        if(isStruct) 
        {
            if(!dependencyNode->type->structType.hasGeneratedDefinition)
            {
                cgLLVMStructFromCheckerType(dependencyNode->type);
                dependencyNode->type->structType.hasGeneratedDefinition = true;
            }
        }
        else if(isUnion)
        {
            if(!dependencyNode->type->unionType.hasGeneratedDefinition)
            {
                cgLLVMUnionFromCheckerType(dependencyNode->type);
                dependencyNode->type->unionType.hasGeneratedDefinition = true;
            }
        }
        else if(isTaggedUnion)
        {
            if(!dependencyNode->type->taggedUnionType.hasGeneratedDefinition)
            {
                cgLLVMTaggedUnionFromCheckerType(dependencyNode->type);
                dependencyNode->type->taggedUnionType.hasGeneratedDefinition = true;
            }
        }
        else if(isTuple)
        {
            if(!dependencyNode->type->tupleType.hasGeneratedDefinition)
            {
                cgLLVMTupleFromCheckerType(dependencyNode->type);
                dependencyNode->type->tupleType.hasGeneratedDefinition = true;
            }
        }
    }
}
void cgLLVMCompleteStructuredType(CheckerType *type)
{
    switch(type->kind)
    {
        case C_TYPE_STRUCT:
        {
            LLVMTypeRef structType = type->backendType;
            size_t membCount = (type->structType.declLL == NULL) ? 0 : type->structType.declLL->first->numItems;
            LLVMTypeRef *membTypes = malloc(membCount * sizeof(LLVMTypeRef));

            size_t index = 0;
            
            ScopedDeclLL *currMembLL = type->structType.declLL;
            if(membCount != 0) currMembLL = currMembLL->first;
            while(currMembLL != NULL)
            {   
                membTypes[index] = cgLLVMCheckerTypeToTypeRef(currMembLL->item->type, false);

                index++;
                currMembLL = currMembLL->next;
            }

            if(!LLVMTypeIsSized(structType)) LLVMStructSetBody(structType, membTypes, membCount, false);
        }break;

        case C_TYPE_UNION:
        {
            LLVMTypeRef structType = type->backendType;

            size_t membCount = 1;
            LLVMTypeRef *membTypes = malloc(membCount * sizeof(LLVMTypeRef));

            LLVMTypeRef typeWihGreatestSize = NULL;
            size_t greatestSize = 0;

            ScopedDeclLL *currMembLL = type->structType.declLL;
            if(membCount != 0) currMembLL = currMembLL->first;
            while(currMembLL != NULL)
            {   
                LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currMembLL->item->type, false);

                if(!LLVMTypeIsSized(t))
                {
                    cgLLVMCompleteStructuredType(currMembLL->item->type);
                }

                size_t sizeOfType = LLVMSizeOfTypeInBits(targetDataRef, t);

                if(sizeOfType > greatestSize)
                {
                    typeWihGreatestSize = t;
                    greatestSize = sizeOfType;
                }

                currMembLL = currMembLL->next;
            }

            membTypes[0] = typeWihGreatestSize;

            if(!LLVMTypeIsSized(structType)) LLVMStructSetBody(structType, membTypes, membCount, false);
        }break;
        
        case C_TYPE_TAGGED_UNION:
        {
            LLVMTypeRef structType = type->backendType;
            LLVMTypeRef membTypes[4] = {

                [1] = cgLLVMCheckerTypeToTypeRef(type->taggedUnionType.tagEnumType, false),
                [2] = LLVMInt64TypeInContext(context),
                [3] = LLVMInt64TypeInContext(context),
            };

            LLVMTypeRef innerUnionType = NULL;

            { //set inner union type
                size_t membCount = 1;
                LLVMTypeRef *membTypes = malloc(membCount * sizeof(LLVMTypeRef));

                LLVMTypeRef typeWihGreatestSize = NULL;
                size_t greatestSize = 0;

                ScopedDeclLL *currMembLL = type->taggedUnionType.declLL;
                if(membCount != 0) currMembLL = currMembLL->first;
                while(currMembLL != NULL)
                {   
                    LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currMembLL->item->type, false);

                    if(!LLVMTypeIsSized(t))
                    {
                        cgLLVMCompleteStructuredType(currMembLL->item->type);
                    }

                    size_t sizeOfType = LLVMSizeOfTypeInBits(targetDataRef, t);

                    if(sizeOfType > greatestSize)
                    {
                        typeWihGreatestSize = t;
                        greatestSize = sizeOfType;
                    }

                    currMembLL = currMembLL->next;
                }

                membTypes[0] = typeWihGreatestSize;

                innerUnionType = LLVMStructTypeInContext(context, membTypes, 1, false);
            }

            membTypes[0] = innerUnionType;

            if(!LLVMTypeIsSized(structType)) LLVMStructSetBody(structType, membTypes, 4, false);
        }break;
        
        case C_TYPE_TUPLE:
        {
            LLVMTypeRef structType = type->backendType;
            size_t membCount = type->tupleType.numTypes;
            LLVMTypeRef *membTypes = malloc(membCount * sizeof(LLVMTypeRef));

            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                membTypes[i] = cgLLVMCheckerTypeToTypeRef(type->tupleType.types[i], false);

            }

            if(!LLVMTypeIsSized(structType)) LLVMStructSetBody(structType, membTypes, membCount, false);
        }break;

        default:
        {
            fprintf(stderr, "Unexpected unsized type, compiler error!: ");
            printCheckerType(type);
            fprintf(stderr, "\n");

        }break;
    }
    

}
LLVMTypeRef cgLLVMStructFromCheckerType(CheckerType *type)
{
    if(type->backendType != NULL) return type->backendType;

    char buf[MAX_IDEN_LEN * 2] = {0};
    sprintf(buf, "%s_%s", type->namespaceName, type->structType.name);
    LLVMTypeRef structType = LLVMStructCreateNamed(context, buf);
    type->backendType = structType;

    cgLLVMCompleteStructuredType(type);

    return structType;
}
LLVMTypeRef cgLLVMUnionFromCheckerType(CheckerType *type)
{
    if(type->backendType != NULL) return type->backendType;
    
    char buf[MAX_IDEN_LEN * 2] = {0};
    sprintf(buf, "%s_%s", type->namespaceName, type->unionType.name);
    LLVMTypeRef structType = LLVMStructCreateNamed(context, buf);
    type->backendType = structType;

    cgLLVMCompleteStructuredType(type);

    return structType;
}
LLVMTypeRef cgLLVMTaggedUnionFromCheckerType(CheckerType *type)
{
    if(type->backendType != NULL) return type->backendType;
    
    char buf[MAX_IDEN_LEN * 2] = {0};
    sprintf(buf, "%s_%s", type->namespaceName, type->taggedUnionType.name);
    LLVMTypeRef structType = LLVMStructCreateNamed(context, buf);
    type->backendType = structType;

    cgLLVMCompleteStructuredType(type);

    return structType;
}
LLVMTypeRef cgLLVMTupleFromCheckerType(CheckerType *type)
{
    if(type->backendType != NULL) return type->backendType;
    
    char buf[MAX_IDEN_LEN * 2] = {0};
    sprintf(buf, "tuple%lld", type->typeId);
    LLVMTypeRef structType = LLVMStructCreateNamed(context, buf);
    type->backendType = structType;

    cgLLVMCompleteStructuredType(type);

    return structType;
}

void cgLLVMProgsDeclValueRefs(ASTProg *p)
{
    if(p->declLL == NULL) return;
    ASTDeclLL *currDecl = p->declLL->first;

    //outputFile = stderr;
    while(currDecl != NULL)
    {
        
        if((currDecl->item->kind == A_DECL_FUNC) || (currDecl->item->kind == A_DECL_OPERATOR_FUNC))
        {
            globalContext.gc.currFuncBeingGenerated = currDecl->item;
        }

        cgLLVMDeclValueRef(currDecl->item);

        if((currDecl->item->kind == A_DECL_FUNC) || (currDecl->item->kind == A_DECL_OPERATOR_FUNC))
        {
            globalContext.gc.currFuncBeingGenerated = NULL;
        }
    
        currDecl = currDecl->next;
    }
}
void cgLLVMDeclValueRef(ASTDecl *decl)
{
    switch(decl->kind)
    {
        case A_DECL_VAR:
        {
            LLVMTypeRef varTypeRef = cgLLVMCheckerTypeToTypeRef(decl->declType, false);

            char buf[MAX_IDEN_LEN * 2] = {0};
            sprintf(buf, "%s_%s", decl->tbl->belongsToNamespace->namespace, decl->var.idenExpr->iden.lexeme);

            LLVMValueRef glob = LLVMAddGlobal(module, varTypeRef, buf);
            LLVMSetAlignment(glob, LLVMABIAlignmentOfType(targetDataRef, varTypeRef));

            //do properly todo
            LLVMSetInitializer(glob, LLVMConstNull(varTypeRef));

            decl->backendValRef = glob;

        }break;
        case A_DECL_FUNC:
        {
            globalContext.gc.currFuncBeingGenerated = decl;

            char buf[MAX_IDEN_LEN * 2] = {0};

            if(CHECK_TYPE_FLAG(decl->declType, TYPE_FOREIGN_FLAG))
            {
                sprintf(buf, "%s", decl->func.iden.lexeme);
            }
            else if(CHECK_TYPE_FLAG(decl->declType, TYPE_EXPORT_AS))
            {
                sprintf(buf, "%s", decl->tags->first->item->paramLL->first->item.lexeme + 1);
                buf[strlen(buf) - 1] = '\0';
            }
            else if(globalContext.gc.generatingTypeMethod)
            {
                sprintf(buf, "%s_%s", allocCheckerTypeName(globalContext.gc.typeWhichTypeMethodIsFor), decl->func.iden.lexeme);
            }
            else sprintf(buf, "%s_%s", decl->declType->namespaceName, decl->func.iden.lexeme);

            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(decl->declType, true);
            LLVMValueRef fnRef = NULL;
            
            bool runtimeSupportFuncExists = false;
            if(CHECK_TYPE_FLAG(decl->declType, TYPE_RUNTIME_SUPPORT_FLAG))
            {
                LLVMValueRef r = LLVMGetNamedFunction(module, buf);

                if(r != NULL)
                {
                    fnRef = r;
                    runtimeSupportFuncExists = true;
                }
                else
                {
                    fnRef = LLVMAddFunction(module, buf, t);
                }
            }
            else fnRef = LLVMAddFunction(module, buf, t);

            if(!runtimeSupportFuncExists)
            {
                cgLLVMAddParameterAttributes(fnRef, decl->declType->funcType.retABIInfo, true, 0);


                { //set parameter names and attrivutes
                    ScopedDeclLL *params = decl->declType->funcType.paramLL;
                    LLVMValueRef fnRefParam = NULL;

                    size_t index = decl->declType->funcType.retABIInfo->startIndex;
                    if(params != NULL)
                    {
                        params = params->first;

                        fnRefParam = LLVMGetParam(fnRef, index);
                    }

                    while(params != NULL)
                    {
                        if(isTypeVariadic(params->item->type))
                        {
                            index += 1;
                            fnRefParam = LLVMGetParam(fnRef, index);
                            params = params->next;
                            continue;
                        }

                        LLVMSetValueName2(fnRefParam, params->item->name.lexeme, strlen(params->item->name.lexeme));

                        cgLLVMAddParameterAttributes(fnRef, params->item->abiInfo, false, index + 1);

                        index += 1;
                        fnRefParam = LLVMGetParam(fnRef, index);
                        params = params->next;
                    }
                }

                LLVMSetFunctionCallConv(fnRef, LLVMCCallConv);
            }
            decl->declType->backendType = t;
            decl->backendValRef = fnRef;
            globalContext.gc.currFuncBeingGenerated = NULL;
        }break;
        case A_DECL_OPERATOR_FUNC:
        {
            globalContext.gc.currFuncBeingGenerated = decl;

            char buf[MAX_IDEN_LEN * 2] = {0};

            sprintf(buf, "%s", decl->operFunc.mangledName);

            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(decl->declType, true);
            LLVMValueRef fnRef = LLVMAddFunction(module, buf, t);
            
            cgLLVMAddParameterAttributes(fnRef, decl->declType->funcType.retABIInfo, true, 0);


            { //set parameter names and attrivutes
                ScopedDeclLL *params = decl->declType->funcType.paramLL;
                LLVMValueRef fnRefParam = NULL;

                size_t index = decl->declType->funcType.retABIInfo->startIndex;
                if(params != NULL)
                {
                    params = params->first;

                    fnRefParam = LLVMGetParam(fnRef, index);
                }

                while(params != NULL)
                {
                    if(isTypeVariadic(params->item->type))
                    {
                        index += 1;
                        fnRefParam = LLVMGetParam(fnRef, index);
                        params = params->next;
                        continue;
                    }

                    LLVMSetValueName2(fnRefParam, params->item->name.lexeme, strlen(params->item->name.lexeme));

                    cgLLVMAddParameterAttributes(fnRef, params->item->abiInfo, false, index + 1);

                    index += 1;
                    fnRefParam = LLVMGetParam(fnRef, index);
                    params = params->next;
                }
            }

            LLVMSetFunctionCallConv(fnRef, LLVMCCallConv);

            decl->declType->backendType = t;
            decl->backendValRef = fnRef;

            globalContext.gc.currFuncBeingGenerated = NULL;
        }break;
        case A_DECL_DECLLIST:
        {
            if(decl->declList.declLL == NULL) return;

            ASTDeclLL *currDecl = decl->declList.declLL->first;

            while(currDecl != NULL)
            {
                cgLLVMDeclValueRef(currDecl->item);
                currDecl = currDecl->next;
            }
        }break;
        case A_DECL_METHOD_BLOCK:
        {
            cgLLVMMethodBlock(decl, true);
        }
    }
    
}

void cgLLVMMethodBlock(ASTDecl *d, bool isSignaturesOnly)
{
    ASTDeclLL *currMethodDecl = d->methodBlock.declLL;
    if(currMethodDecl != NULL)
    {
        currMethodDecl = currMethodDecl->first;
        while(currMethodDecl != NULL)
        {
            if(isSignaturesOnly) cgLLVMDeclSignatureInMethodBlock(d, currMethodDecl->item);
            else cgLLVMDeclInMethodBlock(d, currMethodDecl->item);

            currMethodDecl = currMethodDecl->next;
        }
    }
}
void cgLLVMDeclSignatureInMethodBlock(ASTDecl *methodBlock, ASTDecl *d)
{
    switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            CheckerType *funcType = d->declType;

            if(!CHECK_TYPE_FLAG(funcType, TYPE_FOREIGN_FLAG))
            {
                if(d->kind == A_DECL_FUNC)
                {
                    globalContext.gc.generatingTypeMethod = true;
                    globalContext.gc.typeWhichTypeMethodIsFor = methodBlock->methodBlock.type->checkType;
                    cgLLVMDeclValueRef(d);
                    globalContext.gc.generatingTypeMethod = false;
                    globalContext.gc.typeWhichTypeMethodIsFor = NULL;
                }

            }
        }break;

        case A_DECL_DECLLIST:
        {
            ASTDeclLL *declInDeclList = d->declList.declLL;

            if(declInDeclList != NULL)
            {
                declInDeclList = declInDeclList->first;
                while(declInDeclList != NULL)
                {
                    cgLLVMDeclSignatureInMethodBlock(methodBlock, declInDeclList->item);
                    declInDeclList = declInDeclList->next;
                }
            }
        }break;
    }
}
void cgLLVMDeclInMethodBlock(ASTDecl *methodBlock, ASTDecl *d)
{
     switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            CheckerType *funcType = d->declType;

            if(!CHECK_TYPE_FLAG(funcType, TYPE_FOREIGN_FLAG))
            {
                if(d->kind == A_DECL_FUNC)
                {
                    globalContext.gc.currFuncBeingGenerated = d;

                    globalContext.gc.generatingTypeMethod = true;
                    globalContext.gc.typeWhichTypeMethodIsFor = methodBlock->methodBlock.type->checkType;
                    cgLLVMDecl(d);
                    globalContext.gc.generatingTypeMethod = false;
                    globalContext.gc.typeWhichTypeMethodIsFor = NULL;
                }
            }
        }break;

        case A_DECL_DECLLIST:
        {
            ASTDeclLL *declInDeclList = d->declList.declLL;

            if(declInDeclList != NULL)
            {
                declInDeclList = declInDeclList->first;
                while(declInDeclList != NULL)
                {
                    cgLLVMDeclInMethodBlock(methodBlock, declInDeclList->item);
                    declInDeclList = declInDeclList->next;
                }
            }
        }break;
    }
}

void cgLLVMProg(ASTProg *p)
{
    if(p->declLL == NULL) return;
    ASTDeclLL *currDecl = p->declLL->first;

    //outputFile = stderr;
    while(currDecl != NULL)
    {
        if(((currDecl->item->kind != A_DECL_VAR) && (currDecl->item->kind != A_DECL_CONST)) && (currDecl->item->genParams == NULL))
        { 
            if((currDecl->item->kind == A_DECL_FUNC) || (currDecl->item->kind == A_DECL_OPERATOR_FUNC))
            {
                globalContext.gc.currFuncBeingGenerated = currDecl->item;
            }

            cgLLVMDecl(currDecl->item);

            if((currDecl->item->kind == A_DECL_FUNC) || (currDecl->item->kind == A_DECL_OPERATOR_FUNC))
            {
                globalContext.gc.currFuncBeingGenerated = NULL;
            }
        }
        currDecl = currDecl->next;
    }

    {
        ASTDeclLL *currDeclLL = p->globalMethodBlockDecls;

        if(currDeclLL != NULL)
        {
            currDeclLL = currDeclLL->first;

            while(currDeclLL != NULL)
            {
                cgLLVMMethodBlock(currDeclLL->item, false);
                currDeclLL = currDeclLL->next;
            }
        }
    }
}

void cgLLVMDecl(ASTDecl *decl)
{
    switch(decl->kind)
    {
        case A_DECL_FUNC:
        {
            if(CHECK_TYPE_FLAG(decl->declType, TYPE_FOREIGN_FLAG)) break;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, decl->backendValRef, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);

            //creating a point where all locals are allocated lets llvm do better optimizations
            currAllocPoint = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context), "allocPoint");
            globalContext.gc.currFuncBeingGenerated = decl;

            { //generate param allocs

                ScopedDeclLL *currSD = decl->declType->funcType.paramLL;
                if(currSD != NULL) currSD = currSD->first;

                size_t index = decl->declType->funcType.retABIInfo->startIndex;

                while(currSD != NULL)
                {
                    LLVMValueRef p = NULL;
                    ArgABIInfo *abiInfo = currSD->item->abiInfo;
                    switch(abiInfo->kind)
                    {
                        case ABI_IGNORE:
                        case ABI_DIRECT:
                        {
                            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currSD->item->type, false);
                            p = cgLLVMAllocAligned(t, "param");
                            cgLLVMStore(LLVMGetParam(decl->backendValRef, index), p, t);
                        }break;
                        case ABI_DIRECT_COERCE:
                        {
                            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currSD->item->type, false);
                            p = cgLLVMAllocAligned(t, "param");
                            
                            LLVMTypeRef coarcedType = LLVMIntTypeInContext(context, checkerTypeGetSize(abiInfo->directCoerce.type) * 8);
                            LLVMValueRef btcst = LLVMBuildBitCast(builder, p, LLVMPointerType(coarcedType, 0), "");
                            
                            LLVMBuildStore(builder, LLVMGetParam(decl->backendValRef, index), btcst);
                        }break;
                        case ABI_INDIRECT:
                        {
                            p = LLVMGetParam(decl->backendValRef, index);
                        }break;
                    }

                    index++;
                    currSD->item->mySymEntry->backendValRef = p;
                    currSD = currSD->next;
                }
            }


            cgLLVMBlock(decl->func.block);

            if(LLVMGetBasicBlockTerminator(LLVMGetLastBasicBlock(decl->backendValRef)) == NULL)
            {
                cgLLVMImplicityStmtRet();
            }

            if(LLVMGetInstructionParent(currAllocPoint))
            {
                LLVMInstructionEraseFromParent(currAllocPoint);
                currAllocPoint = NULL;
            }
            
            //LLVMRunFunctionPassManager(fnPassManager, decl->backendValRef);
            //LLVMVerifyFunction(decl->backendValRef, LLVMPrintMessageAction);

            globalContext.gc.currFuncBeingGenerated = NULL;
        }break;

        case A_DECL_OPERATOR_FUNC:
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, decl->backendValRef, "entry");
            LLVMPositionBuilderAtEnd(builder, entry);

            //creating a point where all locals are allocated lets llvm do better optimizations
            currAllocPoint = LLVMBuildAlloca(builder, LLVMInt32TypeInContext(context), "allocPoint");
            globalContext.gc.currFuncBeingGenerated = decl;

            { //generate param allocs

                ScopedDeclLL *currSD = decl->declType->funcType.paramLL;
                if(currSD != NULL) currSD = currSD->first;

                size_t index = decl->declType->funcType.retABIInfo->startIndex;

                while(currSD != NULL)
                {
                    LLVMValueRef p = NULL;
                    ArgABIInfo *abiInfo = currSD->item->abiInfo;
                    switch(abiInfo->kind)
                    {
                        case ABI_IGNORE:
                        case ABI_DIRECT:
                        {
                            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currSD->item->type, false);
                            p = cgLLVMAllocAligned(t, "param");
                            cgLLVMStore(LLVMGetParam(decl->backendValRef, index), p, t);
                        }break;
                        case ABI_DIRECT_COERCE:
                        {
                            LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(currSD->item->type, false);
                            p = cgLLVMAllocAligned(t, "param");
                            
                            LLVMTypeRef coarcedType = LLVMIntTypeInContext(context, checkerTypeGetSize(abiInfo->directCoerce.type) * 8);
                            LLVMValueRef btcst = LLVMBuildBitCast(builder, p, LLVMPointerType(coarcedType, 0), "");
                            
                            LLVMBuildStore(builder, LLVMGetParam(decl->backendValRef, index), btcst);
                        }break;
                        case ABI_INDIRECT:
                        {
                            p = LLVMGetParam(decl->backendValRef, index);
                        }break;
                    }

                    index++;
                    currSD->item->mySymEntry->backendValRef = p;
                    currSD = currSD->next;
                }
            }


            cgLLVMBlock(decl->operFunc.block);

            if(LLVMGetBasicBlockTerminator(LLVMGetLastBasicBlock(decl->backendValRef)) == NULL)
            {
                cgLLVMImplicityStmtRet();
            }

            if(LLVMGetInstructionParent(currAllocPoint))
            {
                LLVMInstructionEraseFromParent(currAllocPoint);
                currAllocPoint = NULL;
            }
            
            //LLVMRunFunctionPassManager(fnPassManager, decl->backendValRef);
            globalContext.gc.currFuncBeingGenerated = NULL;
        }break;
    }
}

void cgLLVMBlock(ASTBlock *block)
{
    if(block == NULL) return;
    
    ASTStmtLL *currStmtLL = block->stmts;
    if(currStmtLL != NULL) currStmtLL = currStmtLL->first;

    while(currStmtLL != NULL)
    {
        cgLLVMStmt(currStmtLL->item);
        currStmtLL = currStmtLL->next;
    }
}

void cgLLVMStmt(ASTStmt *stmt)
{
    switch(stmt->kind)
    {
        case A_STMT_ASSIGN: cgLLVMStmtAssign(stmt); break;
        case A_STMT_DECL: cgLLVMStmtDecl(stmt); break;
        case A_STMT_WHILE: cgLLVMStmtWhile(stmt); break;
        case A_STMT_FOR: cgLLVMStmtFor(stmt); break;
        case A_STMT_MATCH: cgLLVMStmtMatch(stmt); break;
        case A_STMT_IF: cgLLVMStmtIf(stmt); break;
        case A_STMT_BLOCK: cgLLVMBlock(stmt->blockStmt.block); break;
        case A_STMT_RET: cgLLVMStmtRet(stmt); break;
        case A_STMT_EXPR: cgLLVMExpr(stmt->expr.expr); break;
        case A_STMT_BREAK:
        {
            LLVMBuildBr(builder, globalContext.gc.blockToBreakTo); break;
        }break;
        case A_STMT_CONTINUE:
        {
            LLVMBuildBr(builder, globalContext.gc.blockToContinueTo); break;
        }break;
    }
}
void cgLLVMStmtAssign(ASTStmt *stmt)
{
    if(stmt->assign.l->kind == A_EXPR_TUPLE_LITERAL) return;

    bool temp = generatingAssignLHS;
    generatingAssignLHS = true;
    LLVMValueRef lhs = cgLLVMExpr(stmt->assign.l);
    generatingAssignLHS = temp;

    if(lhs != NULL)
    {
        LLVMValueRef rhs = cgLLVMExpr(stmt->assign.r);

        if(rhs != NULL)
        {
            cgLLVMStore(rhs, lhs, cgLLVMCheckerTypeToTypeRef(stmt->assign.l->checkType, false));
        }

    }
}
void cgLLVMStmtDecl(ASTStmt *stmt)
{
    ASTDecl *d = stmt->decl.decl;

    switch(d->kind)
    {
        case A_DECL_VAR:
        {
            if(d->var.idenExpr->kind != A_EXPR_IDEN) break;

            LLVMBasicBlockRef currBlock = LLVMGetInsertBlock(builder);
            LLVMPositionBuilderBefore(builder, currAllocPoint);

            LLVMTypeRef varTyperef = cgLLVMCheckerTypeToTypeRef(d->var.type->checkType, false);
            LLVMValueRef a = cgLLVMAllocAligned(varTyperef, d->var.idenExpr->iden.lexeme);

            LLVMPositionBuilderAtEnd(builder, currBlock);

            if(d->var.initial != NULL)
            {
                LLVMValueRef initialExpr = cgLLVMExpr(d->var.initial);

                if(initialExpr != NULL)
                    cgLLVMStore(initialExpr, a, varTyperef);
            }
            else //initalize to zero
            {
                cgLLVMStore(LLVMConstNull(varTyperef), a, varTyperef);
            }

            d->backendValRef = a;
        }break;
        //todo const
    }
}
void cgLLVMStmtFor(ASTStmt *stmt)
{
    LLVMTypeRef i64llvmtype = cgLLVMCheckerTypeToTypeRef(i64Type, false);
    LLVMValueRef fnRef = globalContext.gc.currFuncBeingGenerated->backendValRef;
    
    LLVMValueRef forLoopIndexValRef = cgLLVMAllocAligned(i64llvmtype, "__forIndex");
    stmt->forStmt.indexSymEntry->backendValRef = forLoopIndexValRef;
    
    LLVMTypeRef forLoopElemTypeRef = cgLLVMCheckerTypeToTypeRef(stmt->forStmt.lExprSymEntry->type, false);
    LLVMValueRef forLoopElemValRef = cgLLVMAllocAligned(forLoopElemTypeRef, "__forElem");
    stmt->forStmt.lExprSymEntry->backendValRef = forLoopElemValRef;

    LLVMBasicBlockRef forCondBlock = LLVMCreateBasicBlockInContext(context, "forCond");
    LLVMBasicBlockRef forEnterBlock = LLVMCreateBasicBlockInContext(context, "forEnter");
    LLVMBasicBlockRef forCounterBlock = LLVMCreateBasicBlockInContext(context, "forCounter");
    LLVMBasicBlockRef forExitBlock = LLVMCreateBasicBlockInContext(context, "forExit");

    size_t numStmtsInForBlock = (stmt->forStmt.block->stmts == NULL) ? 0 : stmt->forStmt.block->stmts->first->numItems;

    //the thing were looping over
    LLVMValueRef loopObj = cgLLVMExpr(stmt->forStmt.r);
    LLVMValueRef loopStartIndex = NULL;
    LLVMValueRef loopEndIndex = NULL;

    if(loopObj == NULL)
    {
        return;
    }
    
    bool isArrayObj = isTypeArray(stmt->forStmt.r->checkType);
    bool isRangeObj = areTypesEqual(stmt->forStmt.r->checkType, rangeType);

    if(isRangeObj)
    {
        loopStartIndex = LLVMBuildLoad(builder, LLVMBuildStructGEP(builder, loopObj, 0, ""), "");
        loopEndIndex = LLVMBuildLoad(builder, LLVMBuildStructGEP(builder, loopObj, 1, ""), "");
    }
    else if(isArrayObj)
    {
        loopStartIndex = LLVMConstInt(i64llvmtype, 0, false); //0
        CheckerType *arrType = stmt->forStmt.r->checkType;

        if(isTypeFixedArray(arrType))
        {
            arrType = (isTypeAliased(arrType)) ? getAliasedTypeBase(arrType) : arrType;

            loopEndIndex = LLVMConstInt(i64llvmtype, arrType->arrayType.size, false);
        }
        else
        {
            arrType = (isTypeAliased(arrType)) ? getAliasedTypeBase(arrType) : arrType;

            LLVMValueRef arrViewLen = LLVMBuildLoad(builder, LLVMBuildStructGEP(builder, loopObj, 0, ""), "");
            loopEndIndex = arrViewLen;
        }
    }

    if((loopStartIndex == NULL) || (loopEndIndex == NULL))
    {
        return;
    }

    cgLLVMStore(loopStartIndex, forLoopIndexValRef, i64llvmtype);
    
    cgLLVMAppendAndSwitchToBlock(fnRef, forCondBlock);

    LLVMIntPredicate opType = 0;

    if(isRangeObj)
    {
        opType = LLVMIntSLE;
        cgLLVMStore(cgLLVMLoad(forLoopIndexValRef, ""), forLoopElemValRef, forLoopElemTypeRef);

    }
    else if(isArrayObj)
    {
        opType = LLVMIntSLT;
        //get arrays element at index
        CheckerType *arrType = stmt->forStmt.r->checkType;

        if(isTypeFixedArray(arrType))
        {
            LLVMValueRef indc[2] = 
            {
                LLVMConstNull(i64llvmtype), //0,
                cgLLVMLoad(forLoopIndexValRef, ""),
            };

            LLVMValueRef ptrToIndexInArrObj = LLVMBuildInBoundsGEP(builder, loopObj, indc, 2, "");
            LLVMValueRef toStore = cgLLVMLoad(ptrToIndexInArrObj, "");
            cgLLVMStore(toStore, forLoopElemValRef, forLoopElemTypeRef);
        }
        else if(isTypeSliceArray(arrType))
        {
            LLVMValueRef ptrToData = LLVMBuildStructGEP(builder, loopObj, 1, "");
            LLVMValueRef dataLoaded = LLVMBuildLoad(builder, ptrToData, "");

            LLVMValueRef dataBtcst = LLVMBuildBitCast(builder, 
                                                      dataLoaded, 
                                                      LLVMPointerType(cgLLVMCheckerTypeToTypeRef(getArrayBase(arrType), false), 0), 
                                                      "");
            LLVMValueRef indc[] = 
            {
                cgLLVMLoad(forLoopIndexValRef, ""),
            };

            LLVMValueRef ptrToIndexInArrObj = LLVMBuildInBoundsGEP(builder, dataBtcst, indc, 1, "");
            cgLLVMStore(LLVMBuildLoad(builder, ptrToIndexInArrObj, ""), forLoopElemValRef, forLoopElemTypeRef);
        }
    }
    
    LLVMValueRef loopCondVal = NULL;
    

    loopCondVal = LLVMBuildICmp(builder, 
                                opType, 
                                cgLLVMLoad(forLoopIndexValRef, ""),
                                loopEndIndex, "");

    LLVMBuildCondBr(builder, loopCondVal, forEnterBlock, forExitBlock);
    
    {
        cgLLVMAppendAndSwitchToBlock(fnRef, forEnterBlock);

        if(numStmtsInForBlock != 0) 
        {
            LLVMBasicBlockRef tempBreak = globalContext.gc.blockToBreakTo;
            LLVMBasicBlockRef tempCont = globalContext.gc.blockToContinueTo;

            globalContext.gc.blockToBreakTo = forExitBlock;
            globalContext.gc.blockToContinueTo = forCounterBlock;

            cgLLVMBlock(stmt->forStmt.block);

            globalContext.gc.blockToBreakTo = tempBreak;
            globalContext.gc.blockToContinueTo = tempCont;
        }

        //check if continue or break br already exists
        if(LLVMGetBasicBlockTerminator(LLVMGetLastBasicBlock(fnRef)) == NULL)
        {
            LLVMBuildBr(builder, forCounterBlock);
        }
    }

    { //for counter block
        cgLLVMAppendAndSwitchToBlock(fnRef, forCounterBlock);

        LLVMValueRef t = LLVMBuildAdd(builder, LLVMBuildLoad(builder, forLoopIndexValRef, ""), 
                                      LLVMConstInt(i64llvmtype, 1, false), "");

        cgLLVMStore(t, forLoopIndexValRef, i64llvmtype);

        LLVMBuildBr(builder, forCondBlock);
    }

    cgLLVMAppendAndSwitchToBlock(fnRef, forExitBlock);
}
void cgLLVMStmtWhile(ASTStmt *stmt)
{
    LLVMValueRef fnRef = globalContext.gc.currFuncBeingGenerated->backendValRef;

    LLVMBasicBlockRef whileCondBlock = LLVMCreateBasicBlockInContext(context, "whileCond");
    LLVMBasicBlockRef whileEnterBlock = LLVMCreateBasicBlockInContext(context, "whileEnter");
    LLVMBasicBlockRef whileExitBlock = LLVMCreateBasicBlockInContext(context, "whileExit");

    size_t numStmtsInWhileBlock = (stmt->whileStmt.block->stmts == NULL) ? 0 : stmt->whileStmt.block->stmts->first->numItems;

    cgLLVMAppendAndSwitchToBlock(fnRef, whileCondBlock);

    LLVMValueRef condRef = cgLLVMExpr(stmt->whileStmt.expr);

    if(condRef == NULL) 
    {
        LLVMDeleteBasicBlock(whileCondBlock);
        return;
    }

    LLVMBuildCondBr(builder, condRef, whileEnterBlock, whileExitBlock);
    
    {
        cgLLVMAppendAndSwitchToBlock(fnRef, whileEnterBlock);

        if(numStmtsInWhileBlock != 0) 
        {
            LLVMBasicBlockRef tempBreak = globalContext.gc.blockToBreakTo;
            LLVMBasicBlockRef tempCont = globalContext.gc.blockToContinueTo;

            globalContext.gc.blockToBreakTo = whileExitBlock;
            globalContext.gc.blockToContinueTo = whileCondBlock;

            cgLLVMBlock(stmt->whileStmt.block);

            globalContext.gc.blockToBreakTo = tempBreak;
            globalContext.gc.blockToContinueTo = tempCont;
        }

        //check if continue or break br already exists
        if(LLVMGetBasicBlockTerminator(LLVMGetLastBasicBlock(fnRef)) == NULL)
        {
            LLVMBuildBr(builder, whileCondBlock);
        }
    }

   cgLLVMAppendAndSwitchToBlock(fnRef, whileExitBlock);

}
void cgLLVMStmtMatch(ASTStmt *stmt)
{
    LLVMValueRef fnRef = globalContext.gc.currFuncBeingGenerated->backendValRef;
    size_t numMatchExprs = stmt->match.matchExprLL->first->numItems;
    LLVMValueRef *exprVals = malloc(sizeof(LLVMValueRef) * numMatchExprs);

    for(size_t i = 0; i < numMatchExprs; i++)
    {
        ASTExpr *expr = stmt->match.matchExprList[i];
        exprVals[i] = cgLLVMExpr(expr);

        if(exprVals[i] == NULL) continue;
        
        LLVMValueRef alloced = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), "matchIden");

        cgLLVMStore(exprVals[i], alloced, cgLLVMCheckerTypeToTypeRef(expr->checkType, false));

        exprVals[i] = alloced;
        stmt->match.matchExprCompilerIdenSymEntry[i]->backendValRef = alloced;
    }

    //cgLLVMBuildLogicalAndOrWithValueS(TOK_OR_LOGOP, exprVals[0], exprVals[0], stmt->match.matchExprList[0]->checkType, stmt->match.matchExprList[0]->checkType);

    ASTMatchArmLL *allMatchArms = stmt->match.matchArmLL;

    if(allMatchArms != NULL)
    {
        size_t numArms = allMatchArms->first->numItems;
        LLVMBasicBlockRef *armBlockRefs = malloc(sizeof(LLVMBasicBlockRef) * numArms);
        LLVMBasicBlockRef *armCondBlockRefs = (numArms == 1) ? NULL : malloc(sizeof(LLVMBasicBlockRef) * numArms);

        { //create a basic block for each arm and condition block, where conditions are evaluated
            ASTMatchArmLL *currMatchArmLL = allMatchArms->first;
            size_t index = 0;
            while(currMatchArmLL != NULL)
            {
                armBlockRefs[index] = LLVMCreateBasicBlockInContext(context, "matchArm");
                
                if(armCondBlockRefs != NULL)
                {
                    //first arm does not have a condition block before it
                    if(index == 0)
                    {
                        armCondBlockRefs[index] = NULL;
                    }
                    else armCondBlockRefs[index] = LLVMCreateBasicBlockInContext(context, "matchCond");
                }
                index++;
                currMatchArmLL = currMatchArmLL->next;
            }
        }

        LLVMBasicBlockRef matchExitBloc = LLVMCreateBasicBlockInContext(context, "matchExit");

        { // emit arms
            ASTMatchArmLL *currMatchArmLL = allMatchArms->first;
            size_t index = 0;
            while(currMatchArmLL != NULL)
            {
                ASTMatchArm *arm = currMatchArmLL->item;
                LLVMBasicBlockRef condBlock = (armCondBlockRefs == NULL) ? NULL : armCondBlockRefs[index];

                LLVMBasicBlockRef nextBlock = (index < numArms - 1) ? armBlockRefs[index + 1] : NULL;
                LLVMBasicBlockRef nextCondBlock = (index < numArms - 1) ? armCondBlockRefs[index + 1] : NULL;
                LLVMBasicBlockRef myBlock = armBlockRefs[index];
                
                if(condBlock != NULL) //dont wanna do a cond block for the first one
                {
                    cgLLVMAppendAndSwitchToBlock(fnRef, condBlock);
                }

                cgLLVMStmtMatchArm(stmt, arm, exprVals, myBlock, nextBlock, nextCondBlock, matchExitBloc);

                index++;
                currMatchArmLL = currMatchArmLL->next;
            }
        }

        cgLLVMAppendAndSwitchToBlock(fnRef, matchExitBloc);
    }
}
void cgLLVMStmtMatchArm(ASTStmt *matchStmt, ASTMatchArm *arm, 
                        LLVMValueRef *exprVals, LLVMBasicBlockRef myBlock, LLVMBasicBlockRef nextBlock, 
                        LLVMBasicBlockRef nextCondBlock, LLVMBasicBlockRef exitBlock)
{
    LLVMValueRef fnRef = globalContext.gc.currFuncBeingGenerated->backendValRef;
    ASTExprLL **armExprLLList = arm->armExprLLs;
    size_t numExprLLLists = arm->armExprLLsCount;

    LLVMValueRef *armOrVals = calloc(numExprLLLists, sizeof(LLVMValueRef));
    LLVMValueRef armCondVal = NULL;

    for(size_t i = 0; i < numExprLLLists; i++)
    {
        LLVMValueRef matchExprVal = exprVals[i];
        ASTExpr *matchExpr = matchStmt->match.matchExprList[i];

        ASTExprLL *currExprLL = armExprLLList[i]->first;
        size_t numExprs = currExprLL->numItems;
        size_t j = 0;
        while(currExprLL != NULL)
        {
            ASTExpr *e = currExprLL->item;

            if(isExprDiscardIdentifier(e)) 
            {
                j++;
                currExprLL = currExprLL->next;
                continue;
            }

            CheckerType *lhsType = matchExpr->checkType;
            if(isTypeTaggedUnion(matchExpr->checkType) && isTypeEnum(e->checkType))
            {
                matchExprVal = LLVMBuildStructGEP(builder, exprVals[i], 1, "");
                lhsType = e->checkType;
            }

            LLVMValueRef rhs = cgLLVMExpr(e);
            LLVMValueRef eq = cgLLVMBuildEqWithValueS(TOK_EQ_RELOP, matchExprVal, rhs, lhsType, e->checkType);

            if(armOrVals[i] == NULL) armOrVals[i] = eq;
            else
            {
                LLVMValueRef or = cgLLVMBuildLogicalAndOrWithValueS(TOK_OR_LOGOP, eq, armOrVals[i], 
                                                                    boolType, 
                                                                    boolType);
                armOrVals[i] = or;
            }

            j++;
            currExprLL = currExprLL->next;
        }

        if(armOrVals[i] != NULL) //no discard
        {
            if(armCondVal == NULL) armCondVal = armOrVals[i];
            else
            {
                LLVMValueRef and = cgLLVMBuildLogicalAndOrWithValueS(TOK_AND_LOGOP, armOrVals[i], armCondVal,
                                                                    boolType, 
                                                                    boolType);

                armCondVal = and;
            }
        }
    }

    LLVMBasicBlockRef nextBlockToBranchTo = (nextBlock == NULL) ? exitBlock : nextCondBlock;
    size_t numStmtsInArmASTBlock = (arm->block->stmts == 0) ? 0 : arm->block->stmts->first->numItems;
    //arm cond is usually NULL when it is an else arm
    if(armCondVal != NULL)
    {
        LLVMBuildCondBr(builder, armCondVal, myBlock, nextBlockToBranchTo);
    }
    else
    {
        LLVMBuildBr(builder, myBlock);
    }

    cgLLVMAppendAndSwitchToBlock(fnRef, myBlock);
    if(arm->taggedUnionAutoCasts != NULL)
    {
        ASTStmtLL *currStmtLL = arm->taggedUnionAutoCasts->first;
        while(currStmtLL != NULL)
        {
            cgLLVMStmt(currStmtLL->item);

            LLVMValueRef l = exprVals[currStmtLL->item->belongsToMatchExprIndex];
            l = cgLLVMPossibleLValueToRValue(l, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(currStmtLL->item->ifTaggedUnionType, false), 0));

            LLVMValueRef toStore =  LLVMBuildBitCast(builder, l, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(currStmtLL->item->decl.decl->var.type->checkType, false), 0), "");

            cgLLVMStore(toStore, currStmtLL->item->decl.decl->backendValRef, cgLLVMCheckerTypeToTypeRef(currStmtLL->item->decl.decl->var.type->checkType, false));

            currStmtLL = currStmtLL->next;
        }
    }

    if(numStmtsInArmASTBlock == 0)
    {
        LLVMBuildBr(builder, exitBlock);
    }
    else
    {
        cgLLVMBlock(arm->block);

        LLVMValueRef terminatorInst = LLVMGetBasicBlockTerminator(myBlock);

        if(terminatorInst == NULL)
        {
            LLVMBuildBr(builder, exitBlock);
        }
    }


}

void cgLLVMStmtIf(ASTStmt *stmt)
{
    LLVMValueRef fnRef = globalContext.gc.currFuncBeingGenerated->backendValRef;
    LLVMValueRef condRef = cgLLVMExpr(stmt->ifStmt.cond);

    LLVMBasicBlockRef exitBlock = LLVMCreateBasicBlockInContext(context, "ifExit");
    LLVMBasicBlockRef thenBlock = exitBlock;
    LLVMBasicBlockRef elseBlock = exitBlock;

    size_t numStmtsInThenBlock = (stmt->ifStmt.block->stmts == NULL) ? 0 : stmt->ifStmt.block->stmts->first->numItems;
    size_t numStmtsInElseBlock = (stmt->ifStmt.elseBlock == NULL) ? 0 : stmt->ifStmt.elseBlock->stmts->first->numItems;

    bool needExitBlock = true;
    if(numStmtsInThenBlock != 0)
    {
        thenBlock = LLVMCreateBasicBlockInContext(context, "ifThen");
    }

    if(numStmtsInElseBlock != 0)
    {
        elseBlock = LLVMCreateBasicBlockInContext(context, "ifElse");
    }

    if(condRef != NULL)
    {
        condRef = cgLLVMPossibleLValueToRValue(condRef, cgLLVMCheckerTypeToTypeRef(boolType, false));
        if(thenBlock != elseBlock)
        {
            LLVMBuildCondBr(builder, condRef, thenBlock, elseBlock);
        }
        else //if then and else blocks have no stmts
        {
            needExitBlock = true;
            LLVMBuildBr(builder, exitBlock);
        }

        bool isThenBlockNeedExit = false;
        bool isElseBlockNeedExit = false;

        if(thenBlock != exitBlock)
        {
            cgLLVMAppendAndSwitchToBlock(fnRef, thenBlock);

            cgLLVMBlock(stmt->ifStmt.block);

            LLVMValueRef terminatorInst = LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder));

            if(terminatorInst == NULL)
            {
                isThenBlockNeedExit = true;
                needExitBlock = true;
                LLVMBuildBr(builder, exitBlock);
            }
            else isThenBlockNeedExit = false;
        }

        if(elseBlock != exitBlock)
        {
            cgLLVMAppendAndSwitchToBlock(fnRef, elseBlock);

            cgLLVMBlock(stmt->ifStmt.elseBlock);

            LLVMValueRef terminatorInst = LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)); //elseBlock);

            if(terminatorInst == NULL)
            {
                isElseBlockNeedExit = true;
                needExitBlock = true;
                LLVMBuildBr(builder, exitBlock);
            }
            else isElseBlockNeedExit = false;
        }

        if(isElseBlockNeedExit || isThenBlockNeedExit || LLVMGetFirstUse(LLVMBasicBlockAsValue(exitBlock)))
        {
            cgLLVMAppendAndSwitchToBlock(fnRef, exitBlock);
        }
    }
}

void cgLLVMImplicityStmtRet()
{
    LLVMTypeRef retType = LLVMGetReturnType(globalContext.gc.currFuncBeingGenerated->declType->backendType);

    if(LLVMGetTypeKind(retType) == LLVMVoidTypeKind)
    {
        LLVMBuildRetVoid(builder);
        return;
    }
    LLVMValueRef v = LLVMConstNull(retType);//LLVMBuildAlloca(builder, retType, "_implicitRet");

    CheckerType *funcType = globalContext.gc.currFuncBeingGenerated->declType;

    ArgABIInfo *retABI = funcType->funcType.retABIInfo;
    LLVMValueRef retExpr = v;

    if(retExpr == NULL) return;

    cgLLVMABIRet(retExpr);
   
}
void cgLLVMStmtRet(ASTStmt *stmt)
{
    if(stmt->retStmt.expr == NULL)
    {
        LLVMBuildRetVoid(builder);
        return;
    }

    CheckerType *funcType = globalContext.gc.currFuncBeingGenerated->declType;

    ArgABIInfo *retABI = funcType->funcType.retABIInfo;
    LLVMValueRef retExpr = cgLLVMExpr(stmt->retStmt.expr);

    if(retExpr == NULL) return;

    cgLLVMABIRet(retExpr);
}
void cgLLVMABIRet(LLVMValueRef exprToRet)
{
    if(exprToRet == NULL)
    {
        LLVMBuildRetVoid(builder);
        return;
    }

    CheckerType *funcType = globalContext.gc.currFuncBeingGenerated->declType;

    ArgABIInfo *retABI = funcType->funcType.retABIInfo;

    switch(retABI->kind)
    {
        case ABI_INDIRECT:
        {
            int64_t size = checkerTypeGetSize(retABI->indirect.type);

            LLVMValueRef retOut = LLVMGetParam(globalContext.gc.currFuncBeingGenerated->backendValRef, 0);

            LLVMTypeRef retExprType = LLVMTypeOf(exprToRet);

            LLVMTypeKind kind = LLVMGetTypeKind(retExprType);
            
            cgLLVMStore(exprToRet, retOut, cgLLVMCheckerTypeToTypeRef(retABI->indirect.type, false));

            //retExpr = NULL;

            LLVMBuildRetVoid(builder);
            return;
        }break;
        case ABI_DIRECT_COERCE:
        {
            int64_t size = checkerTypeGetSize(retABI->directCoerce.type) * 8;

            LLVMValueRef bitcst = cgLLVMBitcast(exprToRet, LLVMPointerType(LLVMIntTypeInContext(context, size), 0));

            exprToRet = LLVMBuildLoad2(builder, LLVMIntTypeInContext(context, size), bitcst, "retExpr");
        }break;

        case ABI_DIRECT:
        case ABI_IGNORE:
        {
            exprToRet = cgLLVMPossibleLValueToRValue(exprToRet, cgLLVMCheckerTypeToTypeRef(funcType->funcType.ret, false));
            break;
        }
    }

    if(exprToRet != NULL)
    {
        LLVMBuildRet(builder, exprToRet);
    }
}

LLVMValueRef cgLLVMExpr(ASTExpr *expr)
{
    if(expr->compTimeVal.kind != A_EXPR_COMP_TIME_RUNTIME)
    {
        switch(expr->compTimeVal.kind)
        {
            case A_EXPR_COMP_TIME_BOOL:
            case A_EXPR_COMP_TIME_INT:
            {
                return LLVMConstInt(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), expr->compTimeVal.i, false);
            }break;

            case A_EXPR_COMP_TIME_FLOAT:
            {
                return LLVMConstReal(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), expr->compTimeVal.f);
            }break;

            case A_EXPR_COMP_TIME_STRING:
            {
                size_t len = 0;
                char *strData = cgLLVMStringToLLVMCompatibleString(expr->compTimeVal.str.data, &len);

                // LLVMValueRef globStrData = LLVMAddGlobal(module, LLVMArrayType(LLVMInt8TypeInContext(context), expr->compTimeVal.str.realLenExcludingEscapedCharacters + 1), "__strData");
                // LLVMSetGlobalConstant(globStrData, true);

                // LLVMSetInitializer(globStrData, LLVMConstStringInContext(context, strData, expr->compTimeVal.str.realLenExcludingEscapedCharacters, false));

                // LLVMSetAlignment(globStrData, LLVMABIAlignmentOfType(targetDataRef, LLVMTypeOf(globStrData)));

                LLVMValueRef globStrData = cgLLVMCreateCStringConstantFromHashmap(globalContext.gc.stringHashMap, strData, len, true);
                LLVMValueRef stringMembs[2] = 
                {
                    LLVMConstBitCast(globStrData, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                    LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false),  expr->compTimeVal.str.realLenExcludingEscapedCharacters, false),
                };

                LLVMValueRef structStrLit = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(stringType, false), "_strLit");

                for(size_t i = 0; i < 2; i ++)
                {
                    LLVMValueRef ptrRef = LLVMBuildStructGEP(builder, structStrLit, i, "");
                    cgLLVMStore(stringMembs[i], ptrRef, LLVMTypeOf(stringMembs[i]));
                }

                return structStrLit;
            }break;
            case A_EXPR_COMP_TIME_NULL:
            {
                return LLVMConstNull(cgLLVMCheckerTypeToTypeRef(expr->checkType, false));
            }break;
        }
        return NULL;
    }

    switch(expr->kind)
    {
        case A_EXPR_LIT:
        {
            switch(expr->lit.type)
            {
                case TOK_BOOL_LITERAL: 
                {
                    bool val = !strcmp("true", expr->lit.lexeme);
                    return LLVMConstInt(cgLLVMCheckerTypeToTypeRef(boolType, false), val, false);
                }break;
                case TOK_INT_LITERAL:
                {
                    int64_t i = stringToInteger(expr->lit.lexeme);
                    return LLVMConstInt(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), i, false);
                }break;

                case TOK_FLOAT_LITERAL:
                {
                    return LLVMConstRealOfString(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), expr->lit.lexeme);
                }break;

                case TOK_STRING_LITERAL: //todo: properley
                {
                    char *strData = expr->compTimeVal.str.data;

                    // LLVMValueRef globStrData = LLVMAddGlobal(module, LLVMArrayType(LLVMInt8TypeInContext(context), expr->compTimeVal.str.len + 1), "__strData");
                    // LLVMSetGlobalConstant(globStrData, true);

                    // LLVMSetInitializer(globStrData, LLVMConstStringInContext(context, strData, expr->compTimeVal.str.len, false));

                    // LLVMSetAlignment(globStrData, LLVMABIAlignmentOfType(targetDataRef, LLVMTypeOf(globStrData)));

                    LLVMValueRef globStrData = cgLLVMCreateCStringConstantFromHashmap(globalContext.gc.stringHashMap, strData, expr->compTimeVal.str.len, true);
                    LLVMValueRef stringMembs[2] = 
                    {
                        LLVMConstBitCast(globStrData, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
                        LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false),  expr->compTimeVal.str.realLenExcludingEscapedCharacters, false),
                    };


                    LLVMValueRef global = LLVMAddGlobal(module, cgLLVMCheckerTypeToTypeRef(expr->checkType, false), "__strLit");
                    LLVMSetGlobalConstant(global, true);

                    LLVMSetInitializer(global, LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), stringMembs, 2));

                    LLVMSetAlignment(global, LLVMABIAlignmentOfType(targetDataRef, LLVMTypeOf(global)));

                    return LLVMBuildLoad(builder, global, "");
                }break;
                case TOK_NULL_KW:
                {
                    return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(context), 0));
                }break;
            }
        }break;
        case A_EXPR_SCOPE_ACCESS:
        case A_EXPR_IDEN:
        {
            if(areTypesEqual(expr->checkType, namespaceInfoType))
            {
                if(expr->idenSymEntry != NULL)
                {
                    if(expr->idenSymEntry->isNamespace || isTypeNamespace(expr->idenSymEntry->type))
                    {
                        return cgLLVMNSTypeNSEntry(expr->idenSymEntry->type);
                    }
                }
            }

            if(expr->idenSymEntry->backendValRef != NULL) return expr->idenSymEntry->backendValRef;
            if(expr->idenSymEntry->myDecl)
            {
                return expr->idenSymEntry->myDecl->backendValRef;
            }
        }break;

        case A_EXPR_FUNC_CALL:
        {
            CheckerType *fnType = expr->funcCall.iden->checkType;
            CheckerType *fnRetType = expr->checkType;
            bool isFuncVariadic = CHECK_TYPE_FLAG(fnType, TYPE_FUNC_VARIADIC_FLAG);
            bool isTypeMethod = fnType->funcType.isTypeMethod;

            ArgABIInfo *retABIInfo = fnType->funcType.retABIInfo;

            if(retABIInfo == NULL) return NULL;

            LLVMValueRef retValRef = NULL;
            LLVMValueRef indirectRet = NULL;
            LLVMValueRef coarcedRet = NULL;

            size_t argCount = expr->funcCall.argCallOrderCount;

            switch(retABIInfo->kind)
            {
                case ABI_DIRECT:
                case ABI_IGNORE: break;
                case ABI_INDIRECT:
                {
                    argCount += 1;
                    indirectRet = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(retABIInfo->indirect.type, false), "_indirectRet");

                    fnRetType = voidType;
                }break;
                case ABI_DIRECT_COERCE:
                {
                    coarcedRet = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(fnRetType, false), "");

                    fnRetType = newCheckerTypeInt(checkerTypeGetSize(retABIInfo->directCoerce.type), 0);

                }break;
            }

            size_t offset = (indirectRet == NULL) ? 0 : 1;

            LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * argCount);

            if(offset == 1) args[0] = indirectRet;

            bool seenVariadicArg = false;
            bool errorOccured = false;
            size_t fnCallArgCount = argCount;
            for(size_t i = 0; i < argCount - offset; i++)
            {
                size_t indexInFn = i + offset;
                ASTExpr *e = expr->funcCall.argCallOrder[i];
                LLVMValueRef argValRef = NULL;

                ScopedDeclLL *paramLL = getScopedDeclLLAt(fnType->funcType.paramLL->first, i);
                ScopedDecl *param = NULL;
                ArgABIInfo *paramABIInfo = NULL;

                if(paramLL != NULL) //might be null if varaidic args
                {
                    param = paramLL->item;
                    paramABIInfo = param->abiInfo;
                    if(isTypeVariadic(param->type)) seenVariadicArg = true;
                }

                if(seenVariadicArg)
                {
                    if(e == NULL) //mpty varags arg
                    {
                        if(!CHECK_TYPE_FLAG(fnType, TYPE_FOREIGN_FLAG)) 
                            argValRef = LLVMGetNamedGlobal(module, "ArrayViewEmpty");
                        else 
                        {
                            fnCallArgCount -= 1;
                            break;
                        }
                    }
                    else argValRef = cgLLVMExpr(e);
                    
                    if(argValRef == NULL)
                    {
                        errorOccured = true;
                        continue;
                    }

                    if(CHECK_TYPE_FLAG(fnType, TYPE_FOREIGN_FLAG))
                    {
                        argValRef = cgLLVMPossibleLValueToRValue(argValRef, cgLLVMCheckerTypeToTypeRef(e->checkType, false));

                        //floats are propmoted to doubles in varadic args in C, hence for them to work they need to be promoted
                        if(LLVMTypeOf(argValRef) == LLVMFloatTypeInContext(context))
                        {
                            argValRef = LLVMBuildFPExt(builder, argValRef, LLVMDoubleTypeInContext(context), "");
                        }
                        args[indexInFn] = argValRef;
                        continue;
                    }
                }
                else 
                {
                    if(isTypeMethod && (i == 0))
                    {
                        e = expr->funcCall.iden->membAccess.typeName;

                        if(CHECK_TYPE_FLAG(fnType, TYPE_FUNC_THISPTR_FLAG))
                        {
                            Token op = expr->startTok;
                            op.type = '*';
                            ASTExpr *ptr = newASTExprUnary(op, e);
                            ptr->checkType = allocCheckerType(C_TYPE_POINTER);
                            ptr->checkType->pointerType.base = expr->funcCall.iden->membAccess.typeName->checkType;
                            ptr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                            ptr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

                            e = ptr;
                        }
                    }
                    //     if(CHECK_TYPE_FLAG(fnType, TYPE_FUNC_THISPTR_FLAG))
                    //     {
                    //         // Token op = expr->startTok;
                    //         // op.type = '*';
                    //         // ASTExpr *ptr = newASTExprUnary(op, expr->funcCall.iden->membAccess.typeName);
                    //         // ptr->checkType = allocCheckerType(C_TYPE_POINTER);
                    //         // ptr->checkType->pointerType.base = expr->funcCall.iden->membAccess.typeName->checkType;
                    //         // ptr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                    //         // ptr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

                    //         if((expr->funcCall.iden->membAccess.typeName->kind == A_EXPR_UNARY) && 
                    //            (expr->funcCall.iden->membAccess.typeName->unary.op.type == '@'))
                    //         {
                    //             argValRef = cgLLVMExpr(expr->funcCall.iden->membAccess.typeName->unary.expr);
                    //         }
                    //         else argValRef = cgLLVMExpr(expr->funcCall.iden->membAccess.typeName);

                    //     }
                    //     else argValRef = cgLLVMExpr(expr->funcCall.iden->membAccess.typeName);
                    // }
                    // else 
                    argValRef = cgLLVMExpr(e);
                }

                if(argValRef == NULL)
                {
                    errorOccured = true;
                    continue;
                }

                switch(paramABIInfo->kind)
                {
                    case ABI_IGNORE:
                    case ABI_DIRECT:
                    {
                        LLVMTypeRef rvaluetype = NULL;
                        if(isTypeMethod && (i == 0))
                        {
                            rvaluetype = cgLLVMCheckerTypeToTypeRef(fnType->funcType.paramLL->first->item->type, false);
                        }
                        else rvaluetype = cgLLVMCheckerTypeToTypeRef(e->checkType, false);
                        argValRef = cgLLVMPossibleLValueToRValue(argValRef, rvaluetype);
                        args[indexInFn] = argValRef;
                    }break;
                    case ABI_DIRECT_COERCE:
                    {
                        CheckerType *destType = newCheckerTypeInt(checkerTypeGetSize(paramABIInfo->directCoerce.type), 0);

                        LLVMValueRef cast = NULL;
                        LLVMTypeRef castToBaseTypeRef = cgLLVMCheckerTypeToTypeRef(destType, false);
                        cast = cgLLVMBitcast(argValRef, LLVMPointerType(castToBaseTypeRef, 0));

                        args[indexInFn] = LLVMBuildLoad2(builder, castToBaseTypeRef, cast, "");
                    }break;
                    case ABI_INDIRECT: 
                    {
                        //argValRef = cgLLVMPossibleLValueToRValue(argValRef, cgLLVMCheckerTypeToTypeRef(e->checkType, false));

                        if(LLVMGetTypeKind(LLVMTypeOf(argValRef)) != LLVMPointerTypeKind)
                        {
                            LLVMValueRef temp = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(paramABIInfo->indirect.type, false), "");

                            cgLLVMStore(argValRef, temp, cgLLVMCheckerTypeToTypeRef(paramABIInfo->indirect.type, false));
                            argValRef = temp;
                        }

                        args[indexInFn] = argValRef;
                    }break;
                }

                if(args[indexInFn] == NULL) errorOccured = true;
            }
            
            LLVMValueRef fnRef = NULL;
            bool allocated = false;
            if(isTypeMethod)
            {
                fnRef = expr->funcCall.iden->idenSymEntry->myDecl->backendValRef;
            }
            else if(expr->funcCall.iden->idenSymEntry == NULL)
            {
                LLVMValueRef tempFnVar = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(expr->funcCall.iden->checkType, false), "");
                //todo store iden into tempfnvar
                LLVMValueRef iden = cgLLVMExpr(expr->funcCall.iden);

                cgLLVMStore(iden, tempFnVar, cgLLVMCheckerTypeToTypeRef(expr->funcCall.iden->checkType, false));

                fnRef = LLVMBuildLoad(builder, tempFnVar, "");
            }
            else 
            {
                fnRef = expr->funcCall.iden->idenSymEntry->myDecl->backendValRef;

                if(expr->funcCall.iden->compTimeVal.isL_or_RValue == EXPR_L_VALUE) //function pointer
                {
                    fnRef = LLVMBuildLoad(builder, fnRef, "");
                }
            }

            //LLVMGetNamedFunction(module, e)
            if(!errorOccured)
            {
                if(fnRef != NULL)
                {
                    LLVMValueRef fncallref = LLVMBuildCall(builder, fnRef, args, fnCallArgCount, "");

                    if(indirectRet != NULL) retValRef = indirectRet;
                    else if(coarcedRet) 
                    {
                        LLVMValueRef btcst = cgLLVMBitcast(coarcedRet, LLVMPointerType(LLVMTypeOf(fncallref), 0));
                        cgLLVMStore(fncallref, btcst, LLVMTypeOf(fncallref));

                        retValRef = coarcedRet;
                    }
                    else retValRef = fncallref;

                    return retValRef;
                }
            }
        }break;
        case A_EXPR_INDEX_REF:
        {
            if(isTypeArray(expr->arrayRef.iden->checkType))
            {
                bool isFixedArray = (isTypeFixedArray(expr->arrayRef.iden->checkType));

                LLVMValueRef val = NULL;

                LLVMTypeRef arrayTypeRef = cgLLVMCheckerTypeToTypeRef(expr->arrayRef.iden->checkType, false);
                LLVMTypeRef arrayBaseTypeRef = NULL;
                if(isFixedArray) arrayBaseTypeRef = LLVMGetElementType(arrayTypeRef);
                else
                {
                    arrayBaseTypeRef = cgLLVMCheckerTypeToTypeRef(getArrayBase(expr->arrayRef.iden->checkType), false);
                }

                LLVMValueRef arrayBeingIndexed = cgLLVMExpr(expr->arrayRef.iden);
                LLVMValueRef arrayIndex = cgLLVMExpr(expr->arrayRef.index);

                arrayIndex = cgLLVMPossibleLValueToRValue(arrayIndex, cgLLVMCheckerTypeToTypeRef(expr->arrayRef.index->checkType, false));

                if((arrayIndex != NULL) && (arrayBeingIndexed != NULL))
                {
                    LLVMValueRef fnCallDataArg = NULL;
                    LLVMValueRef fnCallArrLengthArg = NULL;
                    LLVMValueRef fnCallIndexArg = arrayIndex;
                    LLVMValueRef fnCallElemSizeArg = LLVMSizeOf(arrayBaseTypeRef);

                    if(isFixedArray)
                    {
                        fnCallDataArg = LLVMBuildBitCast(builder, arrayBeingIndexed, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "");
                        fnCallArrLengthArg = LLVMConstInt(LLVMInt64TypeInContext(context), LLVMGetArrayLength(arrayTypeRef), false);
                    }
                    else
                    {
                        LLVMValueRef arrViewData = LLVMBuildStructGEP(builder, arrayBeingIndexed, 1, "");
                        LLVMValueRef arrViewLen = LLVMBuildStructGEP(builder, arrayBeingIndexed, 0, "");

                        fnCallDataArg = LLVMBuildLoad(builder, arrViewData, "");
                        fnCallArrLengthArg = LLVMBuildLoad(builder, arrViewLen, "");
                    }

                    LLVMValueRef sourceLoc = cgLLVMSourceLocLit(expr->startTok.pos);
                    LLVMValueRef fnargs[] = 
                    {
                        fnCallDataArg,
                        fnCallArrLengthArg,
                        fnCallIndexArg,
                        fnCallElemSizeArg,
                        sourceLoc,
                    };

                    LLVMValueRef fncall = LLVMBuildCall(builder, runtimeArrayGetFunc, fnargs, 5, "__arrayGet");

                    val = LLVMBuildBitCast(builder, fncall, LLVMPointerType(arrayBaseTypeRef, 0), "");
                }

                return val;
            }
            else if(isTypePointer(expr->arrayRef.iden->checkType))
            {
                LLVMValueRef val = NULL;

                LLVMTypeRef pointerTypeRef = cgLLVMCheckerTypeToTypeRef(expr->arrayRef.iden->checkType, false);
                LLVMTypeRef pointerBaseTypeRef = LLVMGetElementType(pointerTypeRef);

                LLVMValueRef pointerBeingIndexed = cgLLVMExpr(expr->arrayRef.iden);
                LLVMValueRef pointerIndex = cgLLVMExpr(expr->arrayRef.index);

                pointerBeingIndexed = cgLLVMPossibleLValueToRValue(pointerBeingIndexed, pointerTypeRef);
                pointerIndex = cgLLVMPossibleLValueToRValue(pointerIndex, cgLLVMCheckerTypeToTypeRef(expr->arrayRef.index->checkType, false));

                if((pointerIndex != NULL) && (pointerBeingIndexed != NULL))
                {
                    val = LLVMBuildInBoundsGEP(builder, pointerBeingIndexed, &pointerIndex, 1, "");
                }

                return val;
            }
            else if(isTypeTuple(expr->arrayRef.iden->checkType))
            {
                size_t index = expr->arrayRef.index->compTimeVal.i;

                LLVMValueRef lhs = cgLLVMExpr(expr->arrayRef.iden);
                LLVMTypeRef tupleType = cgLLVMCheckerTypeToTypeRef(expr->arrayRef.iden->checkType, false);

                return LLVMBuildStructGEP(builder, lhs, index, "");
            }
        }break;
        case A_EXPR_NULL_ACCESS:
        {
            ASTExpr *membAccessPreCast = expr->nullAccess.preCastMembAccessExpr;
            ASTExpr *membAccess = expr->nullAccess.access;
            ASTExpr *elseExpr = expr->nullAccess.elseExpr;


            LLVMBasicBlockRef nullBlock = LLVMCreateBasicBlockInContext(context, "nullBlock");
            LLVMBasicBlockRef notNullBlock = LLVMCreateBasicBlockInContext(context, "notNullBlock");
            LLVMBasicBlockRef phiBlock = LLVMCreateBasicBlockInContext(context, "phiBlock");
            LLVMValueRef bckupValueToEmit;

            LLVMValueRef exprToAccess = cgLLVMExpr(membAccessPreCast->membAccess.typeName);

            {
                CheckerType *membAccessType = membAccessPreCast->membAccess.typeName->checkType;
                LLVMTypeRef membAccessTypeRef = cgLLVMCheckerTypeToTypeRef(membAccessType, false);
                LLVMValueRef rhsComp = cgLLVMBitcast(LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(context), 0)), membAccessTypeRef);
                LLVMValueRef compExpr = cgLLVMBuildEqWithValueS(TOK_EQ_RELOP, exprToAccess, rhsComp, membAccessType, membAccessType);

                LLVMValueRef branch = LLVMBuildCondBr(builder, compExpr, nullBlock, notNullBlock);

                cgLLVMAppendAndSwitchToBlock(globalContext.gc.currFuncBeingGenerated->backendValRef, nullBlock);                        

                bckupValueToEmit = (elseExpr == NULL) ? LLVMConstNull(cgLLVMCheckerTypeToTypeRef(membAccessPreCast->checkType, false)):
                                                                                                    cgLLVMExpr(elseExpr);
                if((elseExpr == NULL) && (membAccess->kind == A_EXPR_CAST))
                {
                    bckupValueToEmit = cgLLVMCastExprFromValueRef(membAccess->checkType, membAccess->cast.expr->checkType, bckupValueToEmit);
                }

                bckupValueToEmit = cgLLVMPossibleLValueToRValue(bckupValueToEmit, cgLLVMCheckerTypeToTypeRef(expr->checkType, false));
                
                LLVMBuildBr(builder, phiBlock);

                cgLLVMAppendAndSwitchToBlock(globalContext.gc.currFuncBeingGenerated->backendValRef, notNullBlock);

                {  
                    //has to be before switching blocks to phi  block
                    LLVMValueRef valToRet = cgLLVMMembAccessExprFromValueRef(membAccessPreCast->membAccess.typeName->checkType, exprToAccess, membAccessPreCast->membAccess.memb);
                    if(membAccess->kind == A_EXPR_CAST)
                    {
                        valToRet = cgLLVMCastExprFromValueRef(membAccess->checkType, membAccess->cast.expr->checkType, valToRet);
                    }

                    valToRet = cgLLVMPossibleLValueToRValue(valToRet, cgLLVMCheckerTypeToTypeRef(expr->checkType, false));

                     LLVMBuildBr(builder, phiBlock);

                     cgLLVMAppendAndSwitchToBlock(globalContext.gc.currFuncBeingGenerated->backendValRef, phiBlock);

                    LLVMValueRef phi = LLVMBuildPhi(builder, cgLLVMCheckerTypeToTypeRef(expr->checkType, false), "");

                    LLVMValueRef incVals[2] =
                    {
                        bckupValueToEmit,
                        valToRet,
                    };

                    LLVMBasicBlockRef incBlocks[2] =
                    {
                        nullBlock,
                        notNullBlock,
                    };

                    LLVMAddIncoming(phi, incVals, incBlocks, 2);

                    valToRet = phi;

                    return valToRet;
                }

                char *s = LLVMPrintValueToString(globalContext.gc.currFuncBeingGenerated->backendValRef);
                printf("%s", s);
            }
        }break;
        case A_EXPR_MEMBER_ACCESS:
        {
            return cgLLVMMembAccessExpr(expr->membAccess.typeName, expr->membAccess.memb, expr->membAccess.isMethodAccess);
        }break;
        case A_EXPR_POST:
        {
            LLVMValueRef innerExpr = cgLLVMExpr(expr->post.expr);

            if(innerExpr == NULL) return NULL;

            bool isInc = (expr->post.op.type == TOK_INC_OP) ? true : false;

            CheckerType *exprType = expr->unary.expr->checkType;
            exprType = isTypeAliased(exprType) ? getAliasedTypeBase(exprType) : exprType;

            if(isTypePointer(exprType))
            {
                LLVMValueRef ids = LLVMConstInt(LLVMInt64TypeInContext(context), 1, false);
                if(!isInc) ids = LLVMConstNeg(ids);

                LLVMValueRef beforeOp = cgLLVMLoad(innerExpr, "");
                LLVMValueRef afterOp = LLVMBuildInBoundsGEP(builder, innerExpr, &ids, 1, "");

                cgLLVMStore(afterOp, innerExpr, cgLLVMCheckerTypeToTypeRef(exprType, false));

                return beforeOp;
            }
            else if(isTypeInteger(exprType))
            {
                LLVMValueRef oneConst = LLVMConstInt(cgLLVMCheckerTypeToTypeRef(exprType, false), 1, false);
                LLVMValueRef v = cgLLVMLoad(innerExpr, "");
                
                if(!isInc) oneConst = LLVMConstNeg(oneConst);

                LLVMValueRef afterOp = LLVMBuildAdd(builder, v, oneConst, "");

                cgLLVMStore(afterOp, innerExpr, cgLLVMCheckerTypeToTypeRef(exprType, false));
                return v;
            }
        }break;

        case A_EXPR_BINARY:
        {
            switch(expr->binary.op.type)
            {
                case '?':
                {
                    LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                    LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                    LLVMValueRef rhsComp = (isTypeBoolean(expr->binary.l->checkType)) ? 
                                            LLVMConstInt(cgLLVMCheckerTypeToTypeRef(boolType, false), false, false) :
                                            cgLLVMBitcast(LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(context), 0)), cgLLVMCheckerTypeToTypeRef(expr->binary.l->checkType, false));

                    LLVMValueRef compExpr = cgLLVMBuildEqWithValueS(TOK_EQ_RELOP, lhs, rhsComp, expr->binary.l->checkType, expr->binary.l->checkType);
                    
                    return LLVMBuildSelect(builder, compExpr, rhs, lhs, "nullcoal");
                    // if(isTypeInteger(expr->binary.l->checkType) && isTypeInteger(expr->binary.r->checkType))
                    // {

                    //     if((lhs != NULL) && (rhs != NULL))
                    //     {
                    //         lhs = cgLLVMLoad(lhs, "");
                    //         rhs = cgLLVMLoad(rhs, "");
                            
                    //         if(isAdd) return LLVMBuildAdd(builder, lhs, rhs, "iadd");
                            
                    //         return LLVMBuildSub(builder, lhs, rhs, "isub");
                    //     }
                    // }
                }break;

                case '-':
                case '+': 
                {
                    bool isAdd = (expr->binary.op.type == '+') ? true : false;

                    if(isTypeInteger(expr->binary.l->checkType) && isTypeInteger(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            if(isAdd) return LLVMBuildAdd(builder, lhs, rhs, "iadd");
                            
                            return LLVMBuildSub(builder, lhs, rhs, "isub");
                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType) && isTypeFloat(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            if(isAdd) return LLVMBuildFAdd(builder, lhs, rhs, "fadd");
                            
                            return LLVMBuildFSub(builder, lhs, rhs, "fsub");
                        }
                    }
                    
                    else if(isTypePointer(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(expr->checkType, false));
                            rhs = cgLLVMLoad(rhs, ""); //rhs should be integer

                            if(!isAdd) rhs = LLVMBuildMul(builder, rhs, LLVMConstNeg(LLVMConstInt(cgLLVMCheckerTypeToTypeRef(expr->binary.r->checkType, false), 1, false)), "");

                            LLVMValueRef inds[1] = 
                            {
                                rhs,
                            };

                            return LLVMBuildInBoundsGEP(builder, lhs, inds, 1, "");
                        }
                    }
                    else if(isTypePointer(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, ""); //lhs should be integer

                            LLVMValueRef inds[1] = 
                            {
                                lhs,
                            };

                            return LLVMBuildInBoundsGEP(builder, rhs, inds, 1, "");
                        }
                    }
                }break;

                case '/':
                case '*':
                {
                    bool isMult = expr->binary.op.type == '*';
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            if(isMult)
                                return LLVMBuildMul(builder, lhs, rhs, "imul");
                            else
                            {
                                bool isUnsigned = CHECK_TYPE_FLAG(expr->checkType, TYPE_UNSIGNED_FLAG);

                                if(isUnsigned)
                                {
                                    return LLVMBuildUDiv(builder, lhs, rhs, "uidiv");
                                }
                                else return LLVMBuildSDiv(builder, lhs, rhs, "idiv");
                            }

                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            if(isMult)
                                return LLVMBuildFMul(builder, lhs, rhs, "fmul");
                            else
                                return LLVMBuildFDiv(builder, lhs, rhs, "fdiv");
                        }
                    }
                
                }break;
                
                case '%':
                {
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            bool isUnsigned = CHECK_TYPE_FLAG(expr->checkType, TYPE_UNSIGNED_FLAG);

                            if(isUnsigned)
                            {
                                return LLVMBuildURem(builder, lhs, rhs, "uirem");
                            }
                            else return LLVMBuildSRem(builder, lhs, rhs, "irem");
                        }
                    }
                }break;

                case '>':
                {
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            return LLVMBuildICmp(builder, LLVMIntSGT, lhs, rhs, "isgt");
                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType) && isTypeFloat(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            return LLVMBuildFCmp(builder, LLVMRealOGT, lhs, rhs, "fogt");
                        }
                    }
                }break;

                case TOK_GE_RELOP:
                {
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            return LLVMBuildICmp(builder, LLVMIntSGE, lhs, rhs, "isge");
                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType) && isTypeFloat(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            return LLVMBuildFCmp(builder, LLVMRealOGE, lhs, rhs, "foge");
                        }
                    }
                }break;

                case '<':
                {
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            return LLVMBuildICmp(builder, LLVMIntSLT, lhs, rhs, "islt");
                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType) && isTypeFloat(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            return LLVMBuildFCmp(builder, LLVMRealOLT, lhs, rhs, "folt");
                        }
                    }
                }break;

                case TOK_LE_RELOP:
                {
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            return LLVMBuildICmp(builder, LLVMIntSLE, lhs, rhs, "isle");
                        }
                    }
                    else if(isTypeFloat(expr->binary.l->checkType) && isTypeFloat(expr->binary.r->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            
                            return LLVMBuildFCmp(builder, LLVMRealOLE, lhs, rhs, "fole");
                        }
                    }
                }break;

                case '&':
                case '|':
                case '^':
                {
                    bool isOr = (expr->binary.op.type == '|');
                    bool isXOr = (expr->binary.op.type == '^');
                    if(isTypeInteger(expr->binary.l->checkType) || isTypeBoolean(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            if(isOr)
                                return LLVMBuildOr(builder, lhs, rhs, "ibitwiseor");
                            else if(isXOr)
                                return LLVMBuildXor(builder, lhs, rhs, "ibitwisexor");
                            else return LLVMBuildAnd(builder, lhs, rhs, "ibitwiseand");
                        }
                    }
                }break;

                case TOK_OR_LOGOP:
                case TOK_AND_LOGOP:
                {
                    // LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                    // LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                    // if((lhs != NULL) && (rhs != NULL))
                    // {
                    //     lhs = cgLLVMLoad(lhs, "");
                    //     rhs = cgLLVMLoad(rhs, "");
                    //     return LLVMBuildAnd(builder, lhs, rhs, "band");
                    // }

                    bool isAndOp = expr->binary.op.type == TOK_AND_LOGOP;

                    LLVMValueRef currFn = globalContext.gc.currFuncBeingGenerated->backendValRef;

                    LLVMBasicBlockRef blockStartedWith = NULL;
                    LLVMBasicBlockRef rhsBlock = LLVMCreateBasicBlockInContext(context, (isAndOp) ? "rhsAnd" : "rhsOr");
                    LLVMBasicBlockRef phiBlock = LLVMCreateBasicBlockInContext(context, (isAndOp) ? "phiAnd" : "phiOr");

                    LLVMValueRef lhs =  cgLLVMExpr(expr->binary.l);

                    if((lhs != NULL))
                    {
                        lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(expr->binary.l->checkType, false));

                        if(isAndOp)
                            LLVMBuildCondBr(builder, lhs, rhsBlock, phiBlock);
                        else
                            LLVMBuildCondBr(builder, lhs, phiBlock, rhsBlock);

                        cgLLVMAppendAndSwitchToBlock(currFn, rhsBlock);

                        blockStartedWith = LLVMGetPreviousBasicBlock(rhsBlock);

                        LLVMValueRef rhs =  cgLLVMExpr(expr->binary.r);

                        if(rhs != NULL)
                        {
                            rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(expr->binary.r->checkType, false));

                            LLVMBasicBlockRef blockEndedWith = LLVMGetLastBasicBlock(currFn);

                            LLVMBuildBr(builder, phiBlock);
                            cgLLVMAppendAndSwitchToBlock(currFn, phiBlock);

                            //AND DIDDINT EVAULATE TO TRUE
                            LLVMValueRef valIfFailed = LLVMConstInt(cgLLVMCheckerTypeToTypeRef(expr->checkType, false), (isAndOp) ? false : true, false);
                            LLVMValueRef phiVal = LLVMBuildPhi(builder, cgLLVMCheckerTypeToTypeRef(expr->checkType, false), "phiValue");
                            LLVMBasicBlockRef blcks[] =
                            {
                                blockStartedWith,
                                blockEndedWith,
                            };

                            LLVMValueRef vals[] =
                            {
                                valIfFailed,
                                rhs,
                            };

                            LLVMAddIncoming(phiVal, vals, blcks, 2);

                            return phiVal;
                        }

                    }
                }break;

                case TOK_NEQ_RELOP:
                case TOK_EQ_RELOP:
                {
                    LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                    LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                    if((rhs != NULL) && (lhs != NULL))
                    {
                        return cgLLVMBuildEqWithValueS(expr->binary.op.type, lhs, rhs, expr->binary.l->checkType, expr->binary.r->checkType);
                    }
                }break;

                case TOK_RIGHT_SHIFT:
                case TOK_LEFT_SHIFT:
                {
                    bool isRight = (expr->binary.op.type == TOK_RIGHT_SHIFT);
                    if(isTypeInteger(expr->binary.l->checkType))
                    {
                        LLVMValueRef lhs = cgLLVMExpr(expr->binary.l);
                        LLVMValueRef rhs = cgLLVMExpr(expr->binary.r);

                        bool typeIsUnsigned = CHECK_TYPE_FLAG(expr->binary.l->checkType, TYPE_UNSIGNED_FLAG);

                        if((lhs != NULL) && (rhs != NULL))
                        {
                            lhs = cgLLVMLoad(lhs, "");
                            rhs = cgLLVMLoad(rhs, "");
                            if(isRight)
                            {
                                if(typeIsUnsigned) return LLVMBuildLShr(builder, lhs, rhs, "ilshr");
                                else return LLVMBuildAShr(builder, lhs, rhs, "iashr");
                            }
                            else return LLVMBuildShl(builder, lhs, rhs, "ishl");
                        }
                    }
                }break;
            }
        }break;
        case A_EXPR_UNARY:
        {
            LLVMValueRef innerExpr = cgLLVMExpr(expr->unary.expr);

            if(innerExpr == NULL) return NULL;

            switch(expr->unary.op.type)
            {
                case '+':
                {
                    return innerExpr;
                }break;
                case '-':
                {
                    CheckerType *exprType = expr->unary.expr->checkType;
                    exprType = isTypeAliased(exprType) ? getAliasedTypeBase(exprType) : exprType;

                    LLVMValueRef v = cgLLVMLoad(innerExpr, "");

                    if(isTypeFloat(exprType))
                    {
                        return LLVMBuildFMul(builder, v, LLVMConstReal(cgLLVMCheckerTypeToTypeRef(exprType, false), -1), "");
                    }
                    else
                    {
                        return LLVMBuildMul(builder, v, LLVMConstNeg(LLVMConstInt(cgLLVMCheckerTypeToTypeRef(exprType, false), 1, false)), "");
                    }
                }break;
                case '*':
                {
                    return innerExpr;
                }break;
                case '@':
                {
                    return cgLLVMLoad(innerExpr, "");
                }break;
                case '!':
                {
                    CheckerType *exprType = expr->unary.expr->checkType;
                    exprType = isTypeAliased(exprType) ? getAliasedTypeBase(exprType) : exprType;

                    LLVMValueRef v = cgLLVMLoad(innerExpr, "");

                    if(isTypeBoolean(exprType))
                    {
                        return LLVMBuildNot(builder, v, "bnot");
                    }
                }break;

                case TOK_DEC_OP:
                case TOK_INC_OP:
                {                    
                    bool isInc = (expr->unary.op.type == TOK_INC_OP) ? true : false;

                    CheckerType *exprType = expr->unary.expr->checkType;
                    exprType = isTypeAliased(exprType) ? getAliasedTypeBase(exprType) : exprType;

                    if(isTypePointer(exprType))
                    {
                        LLVMValueRef ids = LLVMConstInt(LLVMInt64TypeInContext(context), 1, false);
                        if(!isInc) ids = LLVMConstNeg(ids);

                        LLVMValueRef afterOp = LLVMBuildInBoundsGEP(builder, innerExpr, &ids, 1, "");

                        return afterOp;
                    }
                    else if(isTypeInteger(exprType))
                    {
                        LLVMValueRef oneConst = LLVMConstInt(cgLLVMCheckerTypeToTypeRef(exprType, false), 1, false);
                        LLVMValueRef v = cgLLVMLoad(innerExpr, "");
                        
                        if(!isInc) oneConst = LLVMConstNeg(oneConst);

                        LLVMValueRef afterOp = LLVMBuildAdd(builder, v, oneConst, "");

                        cgLLVMStore(afterOp, innerExpr, cgLLVMCheckerTypeToTypeRef(exprType, false));
                        return innerExpr;
                    }

                }break;
            }

        }break;
        case A_EXPR_EXPR_INTRO:
        {
            return cgLLVMTypesTypeEntry(expr->exprIntro.expr->checkType);
        }break;
        case A_EXPR_TYPE_INTRO:
        {
            return cgLLVMTypesTypeEntry(expr->typeIntro.type->checkType);
        }break;
        case A_EXPR_STRUCT_LITERAL:
        {
            if(isTypeStruct(expr->checkType))
            {
                return cgLLVMStructLitFromExpr(expr);
            }
            else if(isTypeUnion(expr->checkType))
            {
                //TODO
            }
            else if(isTypeTaggedUnion(expr->checkType))
            {
                return cgLLVMTaggedUnionLitFromExpr(expr);
            }
        }break;
        case A_EXPR_ARRAY_LITERAL:
        {
            LLVMTypeRef arrayTypeRef = cgLLVMCheckerTypeToTypeRef(expr->checkType, false);
            LLVMValueRef val = NULL;
            bool success = true;

            size_t arrayDimen = LLVMGetArrayLength(arrayTypeRef);
            LLVMValueRef *exprRefs = malloc(sizeof(LLVMValueRef) * arrayDimen);

            ASTExprLL *currExprLL = expr->arrayLit.exprLL->first;
            
            size_t index = 0;
            while(currExprLL != NULL)
            {
                exprRefs[index] = cgLLVMExpr(currExprLL->item);

                if(exprRefs[index] == NULL)
                {
                    success = false;
                }

                index++;
                currExprLL = currExprLL->next;
            }

            if(success)
            {
                LLVMValueRef temp = cgLLVMAllocAligned(arrayTypeRef, "__arrayLit");
                LLVMBuildStore(builder, LLVMConstNull(arrayTypeRef), temp);

                for(size_t i = 0; i < index; i++)
                {
                    LLVMValueRef indc[2] = 
                    {
                        LLVMConstNull(LLVMInt64TypeInContext(context)),
                        LLVMConstInt(LLVMInt64TypeInContext(context), i, false),
                    };

                    LLVMValueRef ptr = LLVMBuildInBoundsGEP(builder, temp, indc, 2, "");

                    cgLLVMStore(exprRefs[i], ptr, LLVMGetElementType(arrayTypeRef));
                }

                val = temp;
            }

            return val;
        }break;

        case A_EXPR_IF:
        {
            return expr->idenSymEntry->myDecl->backendValRef;
        }break;

        case A_EXPR_WHILE:
        {
            return expr->idenSymEntry->myDecl->backendValRef;
        }break;

        case A_EXPR_BLOCK:
        {
            return expr->idenSymEntry->myDecl->backendValRef;
        }break;

        case A_EXPR_MATCH:
        {
            return expr->idenSymEntry->myDecl->backendValRef;
        }break;
        case A_EXPR_CAST:
        {
            CheckerType *castToType = expr->checkType;
            return cgLLVMCastExpr(castToType, expr->cast.expr);

        }break;
        case A_EXPR_RANGE:
        {
            return cgLLVMRangeLit(expr);
        }break;

        case A_EXPR_TUPLE_LITERAL:
        {
            LLVMTypeRef tupleType = cgLLVMCheckerTypeToTypeRef(expr->checkType, false);
            LLVMValueRef tempLit = cgLLVMAllocAligned(tupleType, "__tupleLit");

            ASTExprLL *exprLL = expr->tupleLit.exprLL->first;

            size_t index = 0;
            while(exprLL != NULL)
            {
                ASTExpr *e = exprLL->item;
                LLVMValueRef ptr = LLVMBuildStructGEP2(builder, tupleType, tempLit, index, "");
                LLVMValueRef val = cgLLVMExpr(e);

                if(val != NULL)
                {
                    cgLLVMStore(val, ptr, cgLLVMCheckerTypeToTypeRef(e->checkType, false));
                }

                index++;
                exprLL = exprLL->next;
            }
            
            return tempLit;
        }break;
    }

    fprintf(stderr, "\nNull expr in llvm backend, but the program should still compiler, expr will not be generated though.\n");
    return NULL;
}
LLVMValueRef cgLLVMMembAccessExprFromValueRef(CheckerType *lhsType, LLVMValueRef lhs, Token memb)
{
    CheckerType *accessableType = lhsType;

    if(isTypeStruct(accessableType))
    {
        if(lhs != NULL)
        {
            accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

            size_t indexOfMember = 0;
            typeHasMember(accessableType, memb.lexeme, &indexOfMember);

            LLVMValueRef ptrToMemb = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");

            return ptrToMemb;
        }
    }
    else if(isTypeUnion(accessableType))
    {
        if(lhs != NULL)
        {
            accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

            size_t indexOfMember = 0;
            typeHasMember(accessableType, memb.lexeme, &indexOfMember);

            ScopedDecl *membAtIndex = getScopedDeclLLAt(accessableType->unionType.declLL, indexOfMember)->item;

            LLVMValueRef v = LLVMBuildBitCast(builder, lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(membAtIndex->type, false), 0), "unionAccess");

            return v;
        }
    }
    else if(isTypeTaggedUnion(accessableType))
    {
        if(lhs != NULL)
        {
            accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;
            lhs = cgLLVMPossibleLValueToRValue(lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(accessableType, false), 0));

            if(!strcmp("kind", memb.lexeme))
            {
                return LLVMBuildStructGEP(builder, lhs, 1, "");
            }
            else
            {
                CheckerType *enumType = isTypeAliased(accessableType->taggedUnionType.tagEnumType) ? 
                                        getAliasedTypeBase(accessableType->taggedUnionType.tagEnumType) :
                                        accessableType->taggedUnionType.tagEnumType;

                size_t memIndex = 0;
                typeHasMember(enumType, memb.lexeme, &memIndex);

                EnumMemb *em = getEnumMembLLAt(enumType->enumType.membLL, memIndex)->item;
                
                return LLVMConstInt(cgLLVMCheckerTypeToTypeRef(enumType, false), em->val, false);
            }
        }
    }
    else if(isTypePointer(accessableType))
    {   
        if(lhs != NULL)
        {
            accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;
            accessableType = accessableType->pointerType.base;
            LLVMTypeRef expectedLHSType = LLVMPointerType(cgLLVMCheckerTypeToTypeRef(accessableType, false), 0);
            LLVMValueRef valToRet;

            lhs = cgLLVMPossibleLValueToRValue(lhs, expectedLHSType);

            if(isTypeStruct(accessableType))
            {
                size_t indexOfMember = 0;
                typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                valToRet = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");
            }
            else if(isTypeUnion(accessableType))
            {
                size_t indexOfMember = 0;
                typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                ScopedDecl *membAtIndex = getScopedDeclLLAt(accessableType->unionType.declLL, indexOfMember)->item;

                valToRet = LLVMBuildBitCast(builder, lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(membAtIndex->type, false), 0), "unionAccess");
            }
            return valToRet;
        }
    }
    else if(isTypeArray(accessableType))
    {
        if(lhs != NULL)
        {
            accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

            if(isTypeFixedArray(accessableType))
            {
                //todo
            }
            else if(isTypeSliceArray(accessableType))
            {
                size_t indexOfMember = 0;
                typeHasMember(arrayViewType, memb.lexeme, &indexOfMember);

                LLVMValueRef ptrToMemb = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");

                return ptrToMemb;
            }
        }
    }
}
LLVMValueRef cgLLVMMembAccessExpr(ASTExpr *lhsExpr, Token memb, bool isMethodAccess)
{
    if(isMethodAccess)
    {

    }
    else if(isTypeEnum(lhsExpr->checkType))
    {

    }
    else
    {
        CheckerType *accessableType = lhsExpr->checkType;

        if(isTypeStruct(accessableType))
        {
            LLVMValueRef lhs = cgLLVMExpr(lhsExpr);
            if(lhs != NULL)
            {
                accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

                size_t indexOfMember = 0;
                typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                LLVMValueRef ptrToMemb = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");

                return ptrToMemb;
            }
        }
        else if(isTypeUnion(accessableType))
        {
            LLVMValueRef lhs = cgLLVMExpr(lhsExpr);
            if(lhs != NULL)
            {
                accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

                size_t indexOfMember = 0;
                typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                ScopedDecl *membAtIndex = getScopedDeclLLAt(accessableType->unionType.declLL, indexOfMember)->item;

                LLVMValueRef v = LLVMBuildBitCast(builder, lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(membAtIndex->type, false), 0), "unionAccess");

                return v;
            }
        }
        else if(isTypeTaggedUnion(accessableType))
        {
            LLVMValueRef lhs = cgLLVMExpr(lhsExpr);
            if(lhs != NULL)
            {
                accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;
                lhs = cgLLVMPossibleLValueToRValue(lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(accessableType, false), 0));

                if(!strcmp("kind", memb.lexeme))
                {
                    return LLVMBuildStructGEP(builder, lhs, 1, "");
                }
                else
                {
                    CheckerType *enumType = isTypeAliased(accessableType->taggedUnionType.tagEnumType) ? 
                                            getAliasedTypeBase(accessableType->taggedUnionType.tagEnumType) :
                                            accessableType->taggedUnionType.tagEnumType;

                    size_t memIndex = 0;
                    typeHasMember(enumType, memb.lexeme, &memIndex);

                    EnumMemb *em = getEnumMembLLAt(enumType->enumType.membLL, memIndex)->item;
                    
                    return LLVMConstInt(cgLLVMCheckerTypeToTypeRef(enumType, false), em->val, false);
                }
            }
        }
        else if(isTypePointer(accessableType))
        {   
            LLVMValueRef lhs = cgLLVMExpr(lhsExpr);

            if(lhs != NULL)
            {
                accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;
                accessableType = accessableType->pointerType.base;
                LLVMTypeRef expectedLHSType = LLVMPointerType(cgLLVMCheckerTypeToTypeRef(accessableType, false), 0);
                LLVMValueRef valToRet;

                lhs = cgLLVMPossibleLValueToRValue(lhs, expectedLHSType);
                lhsExpr->bckendVal = lhs;

                if(isTypeStruct(accessableType))
                {
                    size_t indexOfMember = 0;
                    typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                    valToRet = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");
                }
                else if(isTypeUnion(accessableType))
                {
                    size_t indexOfMember = 0;
                    typeHasMember(accessableType, memb.lexeme, &indexOfMember);

                    ScopedDecl *membAtIndex = getScopedDeclLLAt(accessableType->unionType.declLL, indexOfMember)->item;

                    valToRet = LLVMBuildBitCast(builder, lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(membAtIndex->type, false), 0), "unionAccess");
                }
                return valToRet;
            }
        }
        else if(isTypeArray(accessableType))
        {
            LLVMValueRef lhs = cgLLVMExpr(lhsExpr);
            if(lhs != NULL)
            {
                accessableType = isTypeAliased(accessableType) ? getAliasedTypeBase(accessableType) : accessableType;

                if(isTypeFixedArray(accessableType))
                {
                    //todo
                }
                else if(isTypeSliceArray(accessableType))
                {
                    size_t indexOfMember = 0;
                    typeHasMember(arrayViewType, memb.lexeme, &indexOfMember);

                    LLVMValueRef ptrToMemb = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");

                    return ptrToMemb;
                }
            }
        }
        // else if(isTypeNamespace(accessableType))
        // {
        //     LLVMValueRef lhs = cgLLVMExpr(expr->membAccess.typeName);
        //     if(lhs != NULL)
        //     {
        //         accessableType = namespaceInfoType;

        //         size_t indexOfMember = 0;
        //         typeHasMember(accessableType, expr->membAccess.memb.lexeme, &indexOfMember);

        //         LLVMValueRef ptrToMemb = LLVMBuildStructGEP(builder, lhs, indexOfMember, "");

        //         return ptrToMemb;
        //     }
        // }
    }
}
LLVMValueRef cgLLVMBuildLogicalAndOrWithValueS(TokType op, LLVMValueRef lhs, LLVMValueRef rhs, CheckerType *lhsType, CheckerType *rhsType)
{
    bool isAndOp = op == TOK_AND_LOGOP;

    LLVMValueRef currFn = globalContext.gc.currFuncBeingGenerated->backendValRef;

    LLVMBasicBlockRef blockStartedWith = NULL;
    LLVMBasicBlockRef rhsBlock = LLVMCreateBasicBlockInContext(context, (isAndOp) ? "rhsAnd" : "rhsOr");
    LLVMBasicBlockRef phiBlock = LLVMCreateBasicBlockInContext(context, (isAndOp) ? "phiAnd" : "phiOr");

    if((lhs != NULL))
    {
        lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));

        if(isAndOp)
            LLVMBuildCondBr(builder, lhs, rhsBlock, phiBlock);
        else
            LLVMBuildCondBr(builder, lhs, phiBlock, rhsBlock);

        cgLLVMAppendAndSwitchToBlock(currFn, rhsBlock);

        blockStartedWith = LLVMGetPreviousBasicBlock(rhsBlock);

        if(rhs != NULL)
        {
            rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(rhsType, false));

            LLVMBasicBlockRef blockEndedWith = LLVMGetLastBasicBlock(currFn);

            LLVMBuildBr(builder, phiBlock);
            cgLLVMAppendAndSwitchToBlock(currFn, phiBlock);

            //AND DIDDINT EVAULATE TO TRUE
            LLVMValueRef valIfFailed = LLVMConstInt(cgLLVMCheckerTypeToTypeRef(boolType, false), (isAndOp) ? false : true, false);
            LLVMValueRef phiVal = LLVMBuildPhi(builder, cgLLVMCheckerTypeToTypeRef(boolType, false), "phiValue");
            LLVMBasicBlockRef blcks[] =
            {
                blockStartedWith,
                blockEndedWith,
            };

            LLVMValueRef vals[] =
            {
                valIfFailed,
                rhs,
            };

            LLVMAddIncoming(phiVal, vals, blcks, 2);

            return phiVal;
        }

    }

    return NULL;
}
LLVMValueRef cgLLVMBuildEqWithValueS(TokType op, LLVMValueRef lhs, LLVMValueRef rhs, CheckerType *lhsType, CheckerType *rhsType)
{
    LLVMIntPredicate intOpCode = (op == TOK_EQ_RELOP) ? LLVMIntEQ : LLVMIntNE;
    LLVMRealPredicate floatOpCode = (op == TOK_EQ_RELOP) ? LLVMRealOEQ : LLVMRealONE;

    if(isTypeInteger(lhsType) || 
                       isTypePointer(lhsType) ||
                       isTypeBoolean(lhsType))
    {
        if((lhs != NULL) && (rhs != NULL))
        {
            lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            return LLVMBuildICmp(builder, intOpCode, lhs, rhs, "ieq");
        }
    }
    else if(isTypeEnum(lhsType))
    {
        if((lhs != NULL) && (rhs != NULL))
        {
            lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            return LLVMBuildICmp(builder, intOpCode, lhs, rhs, "ieq");
        }
    }
    else if(isTypeFloat(lhsType))
    {
        if((lhs != NULL) && (rhs != NULL))
        {
            lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(lhsType, false));
            return LLVMBuildFCmp(builder, floatOpCode, lhs, rhs, "feq");
        }
    }
    else if(isTypeString(lhsType))
    {
        if((lhs != NULL) && (rhs != NULL))
        {
            lhs = cgLLVMPossibleLValueToRValue(lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(lhsType, false), 0));
            rhs = cgLLVMPossibleLValueToRValue(rhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(lhsType, false), 0));

            LLVMValueRef fnArgs[] =
            {
                lhs,
                rhs,
            };

            LLVMValueRef fnRef = LLVMGetNamedFunction(module, (op == TOK_EQ_RELOP) ? STRCMP_PRELUDE_FUNC_NAME : NSTRCMP_PRELUDE_FUNC_NAME);
            
            return LLVMBuildCall(builder, fnRef, fnArgs, 2, "");
        }
    }
    else if(isTypeAggregate(lhsType))
    {
        if((lhs != NULL) && (rhs != NULL))
        {
            lhs = cgLLVMPossibleLValueToRValue(lhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(lhsType, false), 0));
            rhs = cgLLVMPossibleLValueToRValue(rhs, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(lhsType, false), 0));


            LLVMValueRef fnArgs[] =
            {
                cgLLVMAnyLitFromValueRef(lhs, lhsType),
                cgLLVMAnyLitFromValueRef(rhs, rhsType),
            };

            LLVMValueRef fnRef = LLVMGetNamedFunction(module, PRELUDE_EQ_FUNC_NAME);
            
            return LLVMBuildCall(builder, fnRef, fnArgs, 2, "");
        }
    }

    return NULL;
}
LLVMValueRef cgLLVMStructLitFromExpr(ASTExpr *expr)
{
    LLVMTypeRef structType = cgLLVMCheckerTypeToTypeRef(expr->checkType, false);
    LLVMValueRef tempLit = cgLLVMAllocAligned(structType, "__structLit");

    for(size_t i = 0; i < expr->structunionLit.membExprOrderCount; i++)
    {
        ASTNamedExpr *ne = expr->structunionLit.membExprOrder[i];
        LLVMValueRef ptr = LLVMBuildStructGEP2(builder, structType, tempLit, i, "");
        
        if(ne == NULL) 
        {
            LLVMValueRef zeroedconst = LLVMConstNull(LLVMGetElementType(LLVMTypeOf(ptr)));

            cgLLVMStore(zeroedconst, ptr, LLVMGetElementType(LLVMTypeOf(ptr)));
            continue;
        }

        ASTExpr *exprToEmit = NULL;
        switch(ne->kind)
        {
            case A_NAMED_EXPR_NAMED: exprToEmit = ne->named.expr; break;
            case A_NAMED_EXPR_NORMAL: exprToEmit = ne->normal; break;
        }

        LLVMValueRef e = cgLLVMExpr(exprToEmit);

        if(e != NULL)
        {
            e = cgLLVMPossibleLValueToRValue(e, cgLLVMCheckerTypeToTypeRef(exprToEmit->checkType, false));

            cgLLVMStore(e, ptr, cgLLVMCheckerTypeToTypeRef(exprToEmit->checkType, false));
        }
    }

    return tempLit;
}
LLVMValueRef cgLLVMTaggedUnionLitFromExpr(ASTExpr *expr)
{
    LLVMTypeRef taggedUnionType = cgLLVMCheckerTypeToTypeRef(expr->checkType, false);
    LLVMValueRef tempLit = cgLLVMAllocAligned(taggedUnionType, "__tagUnionLit");

    LLVMValueRef ptrToUnion = LLVMBuildStructGEP(builder, tempLit, 0, "");
    LLVMValueRef ptrToKind = LLVMBuildStructGEP(builder, tempLit, 1, "");
    LLVMValueRef ptrToIndexEnum = LLVMBuildStructGEP(builder, tempLit, 2, "ptrToIndexEnum");
    LLVMValueRef ptrToIndexUnion = LLVMBuildStructGEP(builder, tempLit, 3, "");

    CheckerType *tgUnionType = (isTypeAliased(expr->checkType)) ? getAliasedTypeBase(expr->checkType) : expr->checkType;
    CheckerType *enumType = isTypeAliased(tgUnionType->taggedUnionType.tagEnumType) ? getAliasedTypeBase(tgUnionType->taggedUnionType.tagEnumType) : tgUnionType->taggedUnionType.tagEnumType;

    for(size_t i = 0; i < expr->structunionLit.membExprOrderCount; i++)
    {
        ASTNamedExpr *ne = expr->structunionLit.membExprOrder[i];
        
        if(ne == NULL) continue;

        ScopedDecl *sd = getScopedDeclLLAt(tgUnionType->taggedUnionType.declLL, i)->item;

        ASTExpr *exprToEmit = NULL;
        switch(ne->kind)
        {
            case A_NAMED_EXPR_NAMED: exprToEmit = ne->named.expr; break;
            case A_NAMED_EXPR_NORMAL: exprToEmit = ne->normal; break;
        }

        LLVMValueRef e = cgLLVMExpr(exprToEmit);

        if(e != NULL)
        {
            LLVMValueRef btcst = LLVMBuildBitCast(builder, 
                                                  ptrToUnion, 
                                                  LLVMPointerType(cgLLVMCheckerTypeToTypeRef(exprToEmit->checkType, false), 0),
                                                  "");

            cgLLVMStore(e, btcst, cgLLVMCheckerTypeToTypeRef(exprToEmit->checkType, false));

            { //set kind
                size_t indexInEnum = 0;
                typeHasMember(enumType, sd->name.lexeme, &indexInEnum);

                EnumMemb *em = getEnumMembLLAt(enumType->enumType.membLL, indexInEnum)->item;

                LLVMValueRef kindVal = LLVMConstInt(cgLLVMCheckerTypeToTypeRef(enumType, false), em->val, false);
                LLVMValueRef indexInEnumVal = LLVMConstInt(LLVMInt64TypeInContext(context), indexInEnum, false);

                cgLLVMStore(kindVal, ptrToKind, cgLLVMCheckerTypeToTypeRef(enumType, false));
                cgLLVMStore(indexInEnumVal, ptrToIndexEnum, LLVMInt64TypeInContext(context));
            }

            { //set pointer in union
                LLVMValueRef indexInUnionVal = LLVMConstInt(LLVMInt64TypeInContext(context), i, false);
                cgLLVMStore(indexInUnionVal, ptrToIndexUnion, LLVMInt64TypeInContext(context));
            }

        }
    }

    return tempLit;
}

LLVMValueRef cgLLVMArrayViewLitFromValueRef(CheckerType *arrayType, LLVMValueRef valRef)
{
     LLVMValueRef ret = NULL;

    LLVMTypeRef arrViewTypeRef = cgLLVMCheckerTypeToTypeRef(arrayViewType, false);
    LLVMValueRef arrViewLit = cgLLVMAllocAligned(arrViewTypeRef, "__arrView");
    LLVMValueRef dataExpr = valRef;

    CheckerType *arrType = isTypeAliased(arrayType) ? getAliasedTypeBase(arrayType) : arrayType;
    CheckerType *baseType = arrType->arrayType.base;
    
    assert((baseType != NULL)); //baseType is null then expr type was not an array

    LLVMValueRef sizeOfBaseVal = LLVMSizeOf(cgLLVMCheckerTypeToTypeRef(baseType, false));
    LLVMValueRef lenOfArrayView = LLVMConstInt(LLVMInt64TypeInContext(context), arrType->arrayType.size, false);

    if(dataExpr != NULL)
    {
        LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(arrayType, false);
        LLVMValueRef temp = cgLLVMAllocAligned(t, "");
        cgLLVMStore(dataExpr, temp, t);

        // LLVMValueRef inds[1] =
        // {
        //     LLVMConstInt(LLVMInt64TypeInContext(context), 0, false),
        // };
        // LLVMValueRef ptr = LLVMBuildGEP(builder, temp, inds, 1, "");

        LLVMValueRef btst = LLVMBuildBitCast(builder, temp, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "__arrViewData");

        LLVMValueRef lenPtr = LLVMBuildStructGEP(builder, arrViewLit, 0, "");
        LLVMValueRef dataPtr = LLVMBuildStructGEP(builder, arrViewLit, 1, "");
        LLVMValueRef baseSizePtr = LLVMBuildStructGEP(builder, arrViewLit, 2, "");

        cgLLVMStore(lenOfArrayView, lenPtr, LLVMInt64TypeInContext(context));
        cgLLVMStore(btst, dataPtr, LLVMPointerType(LLVMInt8TypeInContext(context), 0));
        cgLLVMStore(sizeOfBaseVal, baseSizePtr, LLVMInt64TypeInContext(context));
    
        ret = arrViewLit;
    }

    return ret;
}

LLVMValueRef cgLLVMArrayViewLit(ASTExpr *expr)
{
     LLVMValueRef ret = NULL;

    switch(expr->kind)
    {
        default: 
        {
            // LLVMTypeRef arrViewTypeRef = cgLLVMCheckerTypeToTypeRef(arrayViewType, false);
            // LLVMValueRef arrViewLit = cgLLVMAllocAligned(arrViewTypeRef, "__arrView");
            // LLVMValueRef dataExpr = cgLLVMExpr(expr);

            // CheckerType *arrType = isTypeAliased(expr->checkType) ? getAliasedTypeBase(expr->checkType) : expr->checkType;
            // CheckerType *baseType = arrType->arrayType.base;
            
            // assert((baseType != NULL)); //baseType is null then expr type was not an array

            // LLVMValueRef sizeOfBaseVal = LLVMSizeOf(cgLLVMCheckerTypeToTypeRef(baseType, false));
            // LLVMValueRef lenOfArrayView = LLVMConstInt(LLVMInt64TypeInContext(context), arrType->arrayType.size, false);

            // if(dataExpr != NULL)
            // {
            //     LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(expr->checkType, false);
            //     LLVMValueRef temp = cgLLVMAllocAligned(t, "");
            //     cgLLVMStore(dataExpr, temp, t);

            //     // LLVMValueRef inds[1] =
            //     // {
            //     //     LLVMConstInt(LLVMInt64TypeInContext(context), 0, false),
            //     // };
            //     // LLVMValueRef ptr = LLVMBuildGEP(builder, temp, inds, 1, "");

            //     LLVMValueRef btst = LLVMBuildBitCast(builder, temp, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "__arrViewData");

            //     LLVMValueRef lenPtr = LLVMBuildStructGEP(builder, arrViewLit, 0, "");
            //     LLVMValueRef dataPtr = LLVMBuildStructGEP(builder, arrViewLit, 1, "");
            //     LLVMValueRef baseSizePtr = LLVMBuildStructGEP(builder, arrViewLit, 2, "");

            //     cgLLVMStore(lenOfArrayView, lenPtr, LLVMInt64TypeInContext(context));
            //     cgLLVMStore(btst, dataPtr, LLVMPointerType(LLVMInt8TypeInContext(context), 0));
            //     cgLLVMStore(sizeOfBaseVal, baseSizePtr, LLVMInt64TypeInContext(context));
            
            //     ret = arrViewLit;
            // }
            
            LLVMValueRef dataExpr = cgLLVMExpr(expr);

            ret = cgLLVMArrayViewLitFromValueRef(expr->checkType, dataExpr);

        }break;
    }

    return ret;
}
LLVMValueRef cgLLVMAnyLit(ASTExpr *expr)
{
    LLVMValueRef ret = NULL;

    switch(expr->kind)
    {
        default: 
        {
            
            LLVMValueRef anyLit = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(anyType, false), "__anyLit");
            LLVMValueRef dataExpr = cgLLVMExpr(expr);

            ret = cgLLVMAnyLitFromValueRef(dataExpr, (isTypeNamespace(expr->checkType)) ? namespaceInfoType : expr->checkType);

        }break;
    }

    return ret;
    // cgWrite(", &");
    // cgExprIntro(expr, false);
    // cgWrite(")");
}
LLVMValueRef cgLLVMAnyLitFromValueRef(LLVMValueRef valRef, CheckerType *exprType)
{
    LLVMValueRef ret = NULL;

            
    LLVMValueRef anyLit = cgLLVMAllocAligned(cgLLVMCheckerTypeToTypeRef(anyType, false), "__anyLit");
    LLVMValueRef dataExpr = valRef;

    if(dataExpr != NULL)
    {
        dataExpr = cgLLVMPossibleLValueToRValue(dataExpr, cgLLVMCheckerTypeToTypeRef(exprType, false));
        LLVMTypeRef t = cgLLVMCheckerTypeToTypeRef(exprType, false);
        LLVMValueRef temp = cgLLVMAllocAligned(t, "");
        cgLLVMStore(dataExpr, temp, t);

        // LLVMValueRef inds[1] =
        // {
        //     LLVMConstInt(LLVMInt64TypeInContext(context), 0, false),
        // };
        // LLVMValueRef ptr = LLVMBuildGEP(builder, temp, inds, 1, "");

        LLVMValueRef btst = LLVMBuildBitCast(builder, temp, LLVMPointerType(LLVMInt8TypeInContext(context), 0), "__anyData");

        LLVMValueRef typeInfoPtr = LLVMBuildStructGEP(builder, anyLit, 0, "");
        LLVMValueRef dataPtr = LLVMBuildStructGEP(builder, anyLit, 1, "");

        cgLLVMStore(btst, dataPtr, LLVMPointerType(LLVMInt8TypeInContext(context), 0));
        
        cgLLVMStore(cgLLVMTypesTypeEntry(exprType), typeInfoPtr, LLVMPointerType(cgLLVMCheckerTypeToTypeRef(typeInfoType, false), 0));

        ret = anyLit;
    }

    return ret;
}

LLVMValueRef cgLLVMRangeLit(ASTExpr *expr)
{
    LLVMTypeRef rangeTypeRef = cgLLVMCheckerTypeToTypeRef(rangeType, false);
    LLVMValueRef rangeLit = cgLLVMAllocAligned(rangeTypeRef, "__rangeLit");

    LLVMValueRef lhs = cgLLVMExpr(expr->range.l);
    LLVMValueRef rhs = cgLLVMExpr(expr->range.r);

    if((lhs != NULL) && (rhs != NULL))
    {
        lhs = cgLLVMPossibleLValueToRValue(lhs, cgLLVMCheckerTypeToTypeRef(expr->range.l->checkType, false));
        rhs = cgLLVMPossibleLValueToRValue(rhs, cgLLVMCheckerTypeToTypeRef(expr->range.r->checkType, false));

        LLVMTypeRef int64llvmtype = LLVMInt64TypeInContext(context);

        if(LLVMTypeOf(lhs) != int64llvmtype)
        {
            lhs = LLVMBuildZExt(builder, lhs, int64llvmtype, "");
        }

        if(LLVMTypeOf(rhs) != int64llvmtype)
        {
            rhs = LLVMBuildZExt(builder, rhs, int64llvmtype, "");
        }

        LLVMValueRef membs[2] = 
        {
            lhs,
            rhs,
        };

        if(LLVMIsConstant(lhs) && LLVMIsConstant(rhs))
        {
            LLVMValueRef toStore = LLVMConstNamedStruct(rangeTypeRef, membs, 2);
            cgLLVMStore(toStore, rangeLit, rangeTypeRef);
        }
        else
        {
            LLVMValueRef startPtr = LLVMBuildStructGEP(builder, rangeLit, 0, "");
            LLVMValueRef endPtr = LLVMBuildStructGEP(builder, rangeLit, 1, "");

            cgLLVMStore(lhs, startPtr, int64llvmtype);
            cgLLVMStore(rhs, endPtr, int64llvmtype);
        }

        return rangeLit;
    }

    return NULL;
}

LLVMValueRef cgLLVMCastExprFromValueRef(CheckerType *castTo, CheckerType *castingFrom, LLVMValueRef valRef)
{
    LLVMValueRef ret = NULL;

    if(areTypesEqual(castingFrom, castTo))
    {
        return valRef;
    }

    if(isTypeSliceArray(castTo))
    {
        ret = cgLLVMArrayViewLitFromValueRef(castingFrom, valRef);
    }
    else if(areTypesEqual(castTo, anyType))
    {
        ret = cgLLVMAnyLitFromValueRef(valRef, castingFrom);
    }
    else
    {
        size_t castingFromSize = checkerTypeGetSize(castingFrom);
        size_t castingToSize = checkerTypeGetSize(castTo);

        bool isCastToIntType = isTypeInteger(castTo) || isTypeEnum(castTo) || isTypeBoolean(castTo);
        bool isCastFromIntType = isTypeInteger(castingFrom) || isTypeEnum(castingFrom) || isTypeBoolean(castingFrom);

        if(isCastToIntType && isCastFromIntType)
        {
            LLVMValueRef v = valRef;

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(castingFromSize < castingToSize)
                {
                    CheckerType *aIType = castingFrom;
                    aIType = (isTypeAliased(aIType)) ? getAliasedTypeBase(aIType) : aIType;

                    CheckerType *bIType = castTo;
                    bIType = (isTypeAliased(bIType)) ? getAliasedTypeBase(bIType) : bIType;

                    bool aIsSigned = !CHECK_TYPE_FLAG(aIType, TYPE_UNSIGNED_FLAG);
                    bool bIsSigned = !CHECK_TYPE_FLAG(bIType, TYPE_UNSIGNED_FLAG);

                    if(!aIsSigned)
                        return LLVMBuildZExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                    else
                        return LLVMBuildSExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else if(castingFromSize > castingToSize)
                {
                    return LLVMBuildTrunc(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else return v;
            }
        }
        
        if(isTypeFloat(castTo) && isTypeFloat(castingFrom))
        {
            LLVMValueRef v = valRef;

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(castingFromSize < castingToSize)
                    return LLVMBuildFPExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else if(castingFromSize > castingToSize)
                {
                    return LLVMBuildFPTrunc(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else return v;
            }
        }

        if(isCastToIntType && isTypeFloat(castingFrom))
        {
            LLVMValueRef v = valRef;
            bool isIntegerUnsigned = CHECK_TYPE_FLAG((isTypeAliased(castTo) ? getAliasedTypeBase(castTo) : castTo), TYPE_UNSIGNED_FLAG);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(isIntegerUnsigned)
                    return LLVMBuildFPToUI(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else
                {
                    return LLVMBuildFPToSI(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
            }
        }

        if(isTypeFloat(castTo) && isCastFromIntType)
        {
            LLVMValueRef v = valRef;
            bool isIntegerUnsigned = CHECK_TYPE_FLAG((isTypeAliased(castingFrom) ? getAliasedTypeBase(castingFrom) : castingFrom), TYPE_UNSIGNED_FLAG);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(isIntegerUnsigned)
                    return LLVMBuildUIToFP(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else
                {
                    return LLVMBuildSIToFP(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
            }
        }

        if(isTypePointerOrFunction(castTo) && isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = valRef;

            if(v != NULL)
            {
                //if(expr->compTimeVal.isL_or_RValue == EXPR_L_VALUE) //if its a var then load it
                {
                    v = cgLLVMPossibleLValueToRValue(v, cgLLVMCheckerTypeToTypeRef(castingFrom, false));
                }

                return LLVMBuildBitCast(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }

        if(isTypePointerOrFunction(castTo) && !isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = valRef;
            
            if(isCastFromIntType)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                return LLVMBuildIntToPtr(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
            else if(isTypeFixedArray(castingFrom))
            {
                return LLVMBuildBitCast(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }

        if(!isTypePointerOrFunction(castTo) && isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = valRef;

            if(isTypeInteger(castTo))
            {
                //if(expr->compTimeVal.isL_or_RValue == EXPR_L_VALUE) //if its a var then load it
                {
                    v = cgLLVMPossibleLValueToRValue(v, cgLLVMCheckerTypeToTypeRef(castingFrom, false));
                }

                return LLVMBuildPtrToInt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }
    }

    return ret;
}
LLVMValueRef cgLLVMCastExpr(CheckerType *castTo, ASTExpr *expr)
{
    LLVMValueRef ret = NULL;
    CheckerType *castingFrom = expr->checkType;

    if(areTypesEqual(castingFrom, castTo))
    {
        return cgLLVMExpr(expr);
    }

    if(isTypeSliceArray(castTo))
    {
        ret = cgLLVMArrayViewLit(expr);
    }
    else if(areTypesEqual(castTo, anyType))
    {
        ret = cgLLVMAnyLit(expr);
    }
    else
    {
        size_t castingFromSize = checkerTypeGetSize(castingFrom);
        size_t castingToSize = checkerTypeGetSize(castTo);

        bool isCastToIntType = isTypeInteger(castTo) || isTypeEnum(castTo) || isTypeBoolean(castTo);
        bool isCastFromIntType = isTypeInteger(castingFrom) || isTypeEnum(castingFrom) || isTypeBoolean(castingFrom);

        if(isCastToIntType && isCastFromIntType)
        {
            LLVMValueRef v = cgLLVMExpr(expr);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(castingFromSize < castingToSize)
                {
                    CheckerType *aIType = castingFrom;
                    aIType = (isTypeAliased(aIType)) ? getAliasedTypeBase(aIType) : aIType;

                    CheckerType *bIType = castTo;
                    bIType = (isTypeAliased(bIType)) ? getAliasedTypeBase(bIType) : bIType;

                    bool aIsSigned = !CHECK_TYPE_FLAG(aIType, TYPE_UNSIGNED_FLAG);
                    bool bIsSigned = !CHECK_TYPE_FLAG(bIType, TYPE_UNSIGNED_FLAG);

                    if(!aIsSigned)
                        return LLVMBuildZExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                    else
                        return LLVMBuildSExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else if(castingFromSize > castingToSize)
                {
                    return LLVMBuildTrunc(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else return v;
            }
        }
        
        if(isTypeFloat(castTo) && isTypeFloat(castingFrom))
        {
            LLVMValueRef v = cgLLVMExpr(expr);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(castingFromSize < castingToSize)
                    return LLVMBuildFPExt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else if(castingFromSize > castingToSize)
                {
                    return LLVMBuildFPTrunc(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
                else return v;
            }
        }

        if(isCastToIntType && isTypeFloat(castingFrom))
        {
            LLVMValueRef v = cgLLVMExpr(expr);
            bool isIntegerUnsigned = CHECK_TYPE_FLAG((isTypeAliased(castTo) ? getAliasedTypeBase(castTo) : castTo), TYPE_UNSIGNED_FLAG);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(isIntegerUnsigned)
                    return LLVMBuildFPToUI(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else
                {
                    return LLVMBuildFPToSI(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
            }
        }

        if(isTypeFloat(castTo) && isCastFromIntType)
        {
            LLVMValueRef v = cgLLVMExpr(expr);
            bool isIntegerUnsigned = CHECK_TYPE_FLAG((isTypeAliased(castingFrom) ? getAliasedTypeBase(castingFrom) : castingFrom), TYPE_UNSIGNED_FLAG);

            if(v != NULL)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                if(isIntegerUnsigned)
                    return LLVMBuildUIToFP(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                else
                {
                    return LLVMBuildSIToFP(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
                }
            }
        }

        if(isTypePointerOrFunction(castTo) && isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = cgLLVMExpr(expr);

            if(v != NULL)
            {
                if(expr->compTimeVal.isL_or_RValue == EXPR_L_VALUE) //if its a var then load it
                {
                    v = cgLLVMPossibleLValueToRValue(v, cgLLVMCheckerTypeToTypeRef(castingFrom, false));
                }

                return LLVMBuildBitCast(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }

        if(isTypePointerOrFunction(castTo) && !isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = cgLLVMExpr(expr);
            
            if(isCastFromIntType)
            {
                if(LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                {
                    v = LLVMBuildLoad(builder, v, "");
                }

                return LLVMBuildIntToPtr(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
            else if(isTypeFixedArray(castingFrom))
            {
                return LLVMBuildBitCast(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }

        if(!isTypePointerOrFunction(castTo) && isTypePointerOrFunction(castingFrom))
        {
            LLVMValueRef v = cgLLVMExpr(expr);

            if(isTypeInteger(castTo))
            {
                if(expr->compTimeVal.isL_or_RValue == EXPR_L_VALUE) //if its a var then load it
                {
                    v = cgLLVMPossibleLValueToRValue(v, cgLLVMCheckerTypeToTypeRef(castingFrom, false));
                }

                return LLVMBuildPtrToInt(builder, v, cgLLVMCheckerTypeToTypeRef(castTo, false), "");
            }
        }
    }

    return ret;
}
LLVMValueRef cgLLVMSourceLocLit(TokenPos pos)
{
    LLVMTypeRef sourceLocStructType = LLVMGetTypeByName2(context, RUNTIME_NS_PREFIX "_SourceLoc");

    LLVMValueRef locLit = cgLLVMAllocAligned(sourceLocStructType, "__loc");

    LLVMValueRef fileStr = NULL;
    
    { //CREATE STRING FOR FILE NAME
        char *strData = pos.filename;
        size_t len = strlen(pos.filename);

        // LLVMValueRef globStrData = LLVMAddGlobal(module, LLVMArrayType(LLVMInt8TypeInContext(context), len + 1), "__strData");
        // LLVMSetGlobalConstant(globStrData, true);

        // LLVMSetInitializer(globStrData, LLVMConstStringInContext(context, strData, len, false));

        // LLVMSetAlignment(globStrData, LLVMABIAlignmentOfType(targetDataRef, LLVMTypeOf(globStrData)));

        LLVMValueRef globStrData = cgLLVMCreateCStringConstantFromHashmap(globalContext.gc.stringHashMap, strData, len, false);
        LLVMValueRef stringMembs[2] = 
        {
            LLVMConstBitCast(globStrData, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
            LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false),  len, false),
        };

        LLVMValueRef structStrLit = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(stringType, false), stringMembs, 2);

        fileStr = structStrLit;
    }

    LLVMValueRef lineNumConst = LLVMConstInt(LLVMInt64TypeInContext(context), pos.linenum, 0);
    LLVMValueRef colNumConst = LLVMConstInt(LLVMInt64TypeInContext(context), pos.colnum, 0);

    LLVMValueRef membs[] =
    {
        fileStr,
        lineNumConst,
        colNumConst,
    };

    cgLLVMStore(LLVMConstNamedStruct(sourceLocStructType, membs, 3), locLit, sourceLocStructType);

    return locLit;
}

void cgLLVMAddParameterAttributes(LLVMValueRef fnRef, ArgABIInfo *abiInfo, bool isReturn, size_t indexOfParameter)
{
    if(abiInfo->attr.signext)
    {
        LLVMAttributeRef ref = LLVMCreateEnumAttribute(context, LLVMGetEnumAttributeKindForName("signext", strlen("signext")), 0);

        LLVMAddAttributeAtIndex(fnRef, (LLVMAttributeIndex)indexOfParameter, ref);
    }

    if(abiInfo->attr.zeroext)
    {
        LLVMAttributeRef ref = LLVMCreateEnumAttribute(context, LLVMGetEnumAttributeKindForName("zeroext", strlen("zeroext")), 0);

        LLVMAddAttributeAtIndex(fnRef, (LLVMAttributeIndex)indexOfParameter, ref);
    }

    switch(abiInfo->kind)
    {
        case ABI_INDIRECT: 
        {
            if(isReturn)
            {
                { //add align
                    LLVMAttributeRef ref = LLVMCreateEnumAttribute(context, LLVMGetEnumAttributeKindForName("align", strlen("align")), abiInfo->indirect.alignment);

                    LLVMAddAttributeAtIndex(fnRef, 1, ref);
                }

                { //add sret
                    LLVMAttributeRef ref = LLVMCreateTypeAttribute(context, LLVMGetEnumAttributeKindForName("sret", strlen("sret")), cgLLVMCheckerTypeToTypeRef(abiInfo->indirect.type, false));

                    LLVMAddAttributeAtIndex(fnRef, 1, ref);
                }
            }
            else
            {
                LLVMAttributeRef ref = LLVMCreateEnumAttribute(context, LLVMGetEnumAttributeKindForName("align", strlen("align")), abiInfo->indirect.alignment);

                LLVMAddAttributeAtIndex(fnRef, (LLVMAttributeIndex)indexOfParameter, ref);
            }
        }break;
        default: break;
    }
}

LLVMTypeRef cgLLVMCheckerTypeToTypeRef(CheckerType *type, bool isGlobalFuncType)
{
    LLVMTypeRef ret;

    if(type->backendType != NULL)
    {
        if(!isGlobalFuncType && (LLVMGetTypeKind(type->backendType) == LLVMFunctionTypeKind))
        {
            return LLVMPointerType(type->backendType, 0);
        }
        return type->backendType;
    }

    switch(type->kind)
    {
        case C_TYPE_INT:
        {
            ret = LLVMIntTypeInContext(context, type->byteSize * 8);
        }break;
        case C_TYPE_FLOAT:
        {
            if(type->byteSize == 8) ret = LLVMDoubleTypeInContext(context);
            else ret = LLVMFloatTypeInContext(context);
        }break;
        case C_TYPE_BOOL:
        {
            ret = LLVMInt1TypeInContext(context);
        }break;
        case C_TYPE_VOID:
        {
            ret = LLVMVoidTypeInContext(context);
        }break;
        case C_TYPE_FUNC:
        {
            LLVMTypeRef retType = NULL;
            ArgABIInfo *retABIInfp = type->funcType.retABIInfo;

            size_t paramCount = (type->funcType.paramLL == NULL) ? 0 : type->funcType.paramLL->first->numItems;
            LLVMTypeRef *paramTypes = malloc(paramCount * sizeof(LLVMTypeRef));

            size_t startIndex = 0;

            { //check return abi info
                switch(retABIInfp->kind)
                {
                    case ABI_INDIRECT:
                    {
                        paramCount += 1;
                        paramTypes = realloc(paramTypes, sizeof(LLVMTypeRef) * paramCount);

                        paramTypes[0] = LLVMPointerType(cgLLVMCheckerTypeToTypeRef(retABIInfp->indirect.type, false), 0);
                        retType = LLVMVoidTypeInContext(context);

                        startIndex += 1;

                        retABIInfp->startIndex = startIndex;
                    }break;
                    case ABI_DIRECT_COERCE:
                    {
                        //this is only applicable for windows 64bit
                        int64_t size = checkerTypeGetSize(retABIInfp->directCoerce.type);

                        CheckerType *intCoercedType = newCheckerTypeInt(size, 0);

                        retType = cgLLVMCheckerTypeToTypeRef(intCoercedType, false);

                    }break;
                    case ABI_DIRECT:
                    case ABI_IGNORE:
                    {
                        retType = cgLLVMCheckerTypeToTypeRef(type->funcType.ret, false);
                    }break;
                    default:
                    {
                        retType = cgLLVMCheckerTypeToTypeRef(type->funcType.ret, false);
                    }break;
                }
            }

            bool isVarArg = false;

            { //create paramTypes
                ScopedDeclLL *currParamLL = type->funcType.paramLL;
                if(currParamLL != NULL) currParamLL = currParamLL->first;

                size_t index = startIndex;
                while(currParamLL != NULL)
                { 
                    ArgABIInfo *paramABI = currParamLL->item->abiInfo;
                    { // parameter abi info
                        LLVMTypeRef paramTypeRef = cgLLVMCheckerTypeToTypeRef(currParamLL->item->type, false);
                        if((paramTypeRef == NULL) && CHECK_TYPE_FLAG(type, TYPE_FOREIGN_FLAG))
                        {
                            isVarArg = true;
                            paramCount -= 1;
                        }
                        else 
                        {
                            switch(paramABI->kind)
                            {
                                case ABI_INDIRECT:
                                {
                                    paramTypes[index] = LLVMPointerType(cgLLVMCheckerTypeToTypeRef(paramABI->indirect.type, false), 0);
                                }break;
                                case ABI_DIRECT_COERCE:
                                {
                                    //this is only applicable for windows 64bit
                                    int64_t size = checkerTypeGetSize(paramABI->directCoerce.type);

                                    CheckerType *intCoercedType = newCheckerTypeInt(size, 0);

                                paramTypes[index] = cgLLVMCheckerTypeToTypeRef(intCoercedType, false);

                                }break;
                                case ABI_DIRECT:
                                case ABI_IGNORE:
                                default:
                                {
                                    paramTypes[index] = cgLLVMCheckerTypeToTypeRef(currParamLL->item->type, false);
                                }break;
                            }
                        }
                    }

                    index++;
                    currParamLL = currParamLL->next;
                }
            }


            ret = LLVMFunctionType(retType, paramTypes, paramCount, isVarArg);

            if(!isGlobalFuncType) ret = LLVMPointerType(ret, 0);
        }break;
        case C_TYPE_POINTER:
        {
            if(isTypeVoidPtr(type))
            {
                ret = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
            }
            else ret = LLVMPointerType(cgLLVMCheckerTypeToTypeRef(type->pointerType.base, false), 0);
        }break;
        case C_TYPE_ARRAY:
        {
            if(isTypeFixedArray(type))
            {
                ret = LLVMArrayType(cgLLVMCheckerTypeToTypeRef(type->arrayType.base, false), type->arrayType.size);
            }
            else
            {
                //todo slices;
                if((globalContext.gc.currFuncBeingGenerated != NULL) && CHECK_TYPE_FLAG(globalContext.gc.currFuncBeingGenerated->declType, TYPE_FOREIGN_FLAG))
                {
                    ret = NULL;
                }
                else ret = cgLLVMCheckerTypeToTypeRef(arrayViewType, false);
            }
        }break;

        case C_TYPE_ENUM:
        {
            ret = LLVMInt32TypeInContext(context);
        }break;
        case C_TYPE_ALIASED:
        {
            ret = cgLLVMCheckerTypeToTypeRef(type->aliasedType.base, false);
        }break;

        case C_TYPE_STRUCT:
        {
            ret = cgLLVMStructFromCheckerType(type);
        }break;

        case C_TYPE_UNION:
        {
            ret = cgLLVMUnionFromCheckerType(type);
        }break;

        case C_TYPE_TUPLE:
        {
            ret = cgLLVMTupleFromCheckerType(type);
        }break;

        case C_TYPE_TAGGED_UNION:
        {
            ret = cgLLVMTaggedUnionFromCheckerType(type);
        }break;
        case C_TYPE_NAMESPACE:
        {
            ret = cgLLVMCheckerTypeToTypeRef(namespaceInfoType, false);
        }break;
        default:
        {
            ret = LLVMInt128TypeInContext(context);
        }
    }

    return ret;
}

void cgLLVMAppendAndSwitchToBlock(LLVMValueRef fnRef, LLVMBasicBlockRef block)
{
    LLVMAppendExistingBasicBlock(fnRef, block);
    LLVMPositionBuilderAtEnd(builder, block);

    LLVMBasicBlockRef prevBlock = LLVMGetPreviousBasicBlock(block);
    //block is not well formed
    if(LLVMGetBasicBlockTerminator(prevBlock) == NULL)
    {
        LLVMPositionBuilderAtEnd(builder, prevBlock);
        LLVMBuildBr(builder, block);
        LLVMPositionBuilderAtEnd(builder, block);
    }

}
LLVMValueRef cgLLVMNSTypeNSEntry(CheckerType *type)
{
    LLVMValueRef nsTableGlobal = LLVMGetNamedGlobal(module, "__namespaceTable");

    LLVMValueRef inds[2] =
    {
        LLVMConstInt(LLVMInt64TypeInContext(context), 0, false),
        LLVMConstInt(LLVMInt64TypeInContext(context), type->namespaceType.tble->belongsToNamespace->indexInNamespaceTable, false),
    };
    
    LLVMValueRef v = LLVMBuildInBoundsGEP(builder, nsTableGlobal, inds, 2, "");

    return v;
}
LLVMValueRef cgLLVMTypesTypeEntry(CheckerType *type)
{
    LLVMValueRef typeTableGlobal = LLVMGetNamedGlobal(module, "__typeTable");

    LLVMValueRef inds[2] =
    {
        LLVMConstInt(LLVMInt64TypeInContext(context), 0, false),
        LLVMConstInt(LLVMInt64TypeInContext(context), type->typeId, false),
    };
    
    LLVMValueRef v = LLVMBuildInBoundsGEP(builder, typeTableGlobal, inds, 2, "");

    return v;
}
LLVMValueRef cgLLVMPossibleLValueToRValue(LLVMValueRef val, LLVMTypeRef rvalueType)
{
    //global consts are all pointers
    bool isGlobalConst = LLVMIsAGlobalObject(val);

    if(isGlobalConst && (LLVMTypeOf(val) != rvalueType))
    {
        return LLVMBuildLoad(builder, val, "");
    }
    // else if(LLVMIsConstant(val))
    // {
    //         if(LLVMTypeOf(val) == rvalueType) return val;
    // }

    if(LLVMTypeOf(val) == rvalueType) return val;

    return LLVMBuildLoad2(builder, rvalueType, val, "");
}
LLVMValueRef cgLLVMAlloc(LLVMTypeRef type, char *name, size_t alignment)
{
    LLVMBasicBlockRef currBlock = LLVMGetInsertBlock(builder);

    if(currAllocPoint != NULL) LLVMPositionBuilderBefore(builder, currAllocPoint);

    LLVMValueRef a = LLVMBuildAlloca(builder, type, name);
    LLVMSetAlignment(a, alignment);

    LLVMPositionBuilderAtEnd(builder, currBlock);

    return a;
}
LLVMValueRef cgLLVMAllocAligned(LLVMTypeRef type, char *name)
{
    return cgLLVMAlloc(type, name, LLVMABIAlignmentOfType(targetDataRef, type));
}
LLVMValueRef cgLLVMBitcast(LLVMValueRef val, LLVMTypeRef target)
{
    if(LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind)
    {
        LLVMValueRef v = cgLLVMAllocAligned(LLVMTypeOf(val), "");
        
        cgLLVMStore(val, v, LLVMTypeOf(val));

        val = v;
    }

    if(LLVMIsConstant(val))
    {
        return LLVMConstBitCast(val, target);
    }
    else return LLVMBuildBitCast(builder, val, target, "");
}
LLVMValueRef cgLLVMStore(LLVMValueRef toStore, LLVMValueRef ptr, LLVMTypeRef storageType)
{
    LLVMTypeRef llvmVoidPtr = LLVMPointerType(LLVMInt8TypeInContext(context), 0); 
    
    // if((llvmVoidPtr == LLVMTypeOf(toStore)) && (LLVMTypeOf(toStore) != LLVMTypeOf(ptr)))
    // {
    //     return LLVMBuildStore(builder, LLVMConstNull(LLVMGetElementType(LLVMTypeOf(ptr))), ptr);
    // }
    // else 
    if(LLVMTypeOf(toStore) == LLVMTypeOf(ptr))
    {
        size_t alignment = LLVMABIAlignmentOfType(targetDataRef, storageType);
        LLVMValueRef alignmentAsValRef = LLVMConstInt(LLVMInt64TypeInContext(context), alignment, false);

        size_t size = LLVMABISizeOfType(targetDataRef, storageType);
        return LLVMBuildMemCpy(builder, 
                        ptr, alignment,
                        toStore, alignment, 
                        LLVMConstInt(LLVMInt64TypeInContext(context), size, false));
    }
    else
    {
        return LLVMBuildStore(builder, toStore, ptr);
    }
}
LLVMValueRef cgLLVMLoad(LLVMValueRef toLoad, char *name)
{
    if(LLVMGetTypeKind(LLVMTypeOf(toLoad)) != LLVMPointerTypeKind)
    return toLoad;

    return LLVMBuildLoad(builder, toLoad, name);
}

LLVMValueRef cgLLVMCreateCStringConstantFromHashmap(HashMapString *hms, char *str, size_t len, bool convertEscapeCharacters)
{
    LLVMValueRef globStrData = NULL;
    size_t strDataLen = (convertEscapeCharacters) ? 0 : len;
    char *strData = (convertEscapeCharacters) ? cgLLVMStringToLLVMCompatibleString(str, &strDataLen) : str;

    uint64_t key = fnv1(strData, strDataLen);

    int64_t numInChain = 0;
    HashMapStringEntryChainLL *stringHashmapEntry = GetEntryChainNodeHashMapStringByVal(globalContext.gc.stringHashMap, key, strData, &numInChain);
    if(stringHashmapEntry == NULL)
    {
        AddToHashMapString(globalContext.gc.stringHashMap, key, strData);
        stringHashmapEntry = GetEntryChainNodeHashMapStringByVal(globalContext.gc.stringHashMap, key, strData, &numInChain);
        
        char buf[BUFSIZ + 1];
        sprintf(buf, "__strdata%llu_%llu", key, numInChain);

        globStrData = LLVMAddGlobal(module, LLVMArrayType(LLVMInt8TypeInContext(context), strDataLen + 1), buf);
        LLVMSetGlobalConstant(globStrData, true);

        LLVMSetInitializer(globStrData, LLVMConstStringInContext(context, strData, strDataLen, false));

        LLVMSetAlignment(globStrData, LLVMABIAlignmentOfType(targetDataRef, LLVMTypeOf(globStrData)));

    }
    else
    {
        char buf[BUFSIZ + 1];
        sprintf(buf, "__strdata%llu_%llu", key, numInChain);
        globStrData = LLVMGetNamedGlobal(module, buf);
    }

    return globStrData;
}
LLVMValueRef cgLLVMCreateStringConstantAndGlobal(char *str, size_t len, bool convertEscapeCharacter)
{
    LLVMValueRef globStrData = cgLLVMCreateCStringConstantFromHashmap(globalContext.gc.stringHashMap, str, len, convertEscapeCharacter);
    LLVMValueRef stringMembs[2] = 
    {
        LLVMConstBitCast(globStrData, LLVMPointerType(LLVMInt8TypeInContext(context), 0)),
        LLVMConstInt(cgLLVMCheckerTypeToTypeRef(i64Type, false),  len, false),
    };

    LLVMValueRef structStrLit = LLVMConstNamedStruct(cgLLVMCheckerTypeToTypeRef(stringType, false), stringMembs, 2);

    return structStrLit;
}
char *cgLLVMStringToLLVMCompatibleString(char *str, size_t *outLen)
{
    char buf[MAX_IDEN_LEN] = {0};

    char *ch = str;
    size_t len = 0;

    if(ch == NULL) len += 1;

    else 
    {
        while(*ch != '\0')
        {
            char chToWrite = *ch;
            bool foundSash = false;
            if(*ch == '\\') 
            {
                ch += 1;
                foundSash = true;
                char currCh = *ch;
                if(currCh == '\\')
                {
                    chToWrite = '\\';
                }
                else if(currCh == 'n')
                {
                    chToWrite = '\n';
                }
                else if(currCh == 't')
                {
                    chToWrite = '\t';
                }
                else if(currCh == '0')
                {
                    chToWrite = '\0';
                }
                else chToWrite = currCh;

                ch += 1;
            }

            buf[len++] = chToWrite;
            if(!foundSash) ch++;
        }
    }
    
    char *retStr = malloc(len + 1);
    strcpy(retStr, buf);

    *outLen = len;
    return retStr;
}
