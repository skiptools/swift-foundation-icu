// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
//
//  Copyright (C) 2002-2011, International Business Machines Corporation and others.
//  All Rights Reserved.
//
//*********************************************************************
//  Legacy version from ICU 57, only for use by Apple RuleBasedTokenizer
//  originally added per rdar://37249396 Add ICU 57 version of RBBI classes,
//  urbtok57 interfaces for access via RBT, and better tests
//*********************************************************************

#include <_foundation_unicode/utypes.h>

#if !UCONFIG_NO_BREAK_ITERATION

#include <_foundation_unicode/brkiter.h>
#include <_foundation_unicode/ubrk.h>
#include <_foundation_unicode/unistr.h>
#include <_foundation_unicode/uniset.h>
#include <_foundation_unicode/uchar.h>
#include <_foundation_unicode/uchriter.h>
#include <_foundation_unicode/parsepos.h>
#include <_foundation_unicode/parseerr.h>
#include "cmemory.h"
#include "cstring.h"

#include "rbbidata57.h"
#include "rbbirb57.h"
#include "rbbisetb57.h"
#include "rbbi57.h"
#include "rbbinode.h"
#include "rbbiscan57.h"
#include "rbbitblb57.h"


U_NAMESPACE_BEGIN


//----------------------------------------------------------------------------------------
//
//  Constructor.
//
//----------------------------------------------------------------------------------------
RBBIRuleBuilder57::RBBIRuleBuilder57(const UnicodeString   &rules,
                                       UParseError     *parseErr,
                                       UErrorCode      &status)
 : fRules(rules)
{
    fStatus = &status; // status is checked below
    fParseError = parseErr;
    fDebugEnv   = NULL;
#ifdef RBBI_DEBUG
    fDebugEnv   = getenv("U_RBBIDEBUG");
#endif


    fForwardTree        = NULL;
    fReverseTree        = NULL;
    fSafeFwdTree        = NULL;
    fSafeRevTree        = NULL;
    fDefaultTree        = &fForwardTree;
    fForwardTables      = NULL;
    fReverseTables      = NULL;
    fSafeFwdTables      = NULL;
    fSafeRevTables      = NULL;
    fRuleStatusVals     = NULL;
    fChainRules         = false;
    fLBCMNoChain        = false;
    fLookAheadHardBreak = false;
    fRINoChain          = false;
    fUSetNodes          = NULL;
    fRuleStatusVals     = NULL;
    fScanner            = NULL;
    fSetBuilder         = NULL;
    if (parseErr) {
        uprv_memset(parseErr, 0, sizeof(UParseError));
    }

    if (U_FAILURE(status)) {
        return;
    }

    fUSetNodes          = new UVector(status); // bcos status gets overwritten here
    fRuleStatusVals     = new UVector(status);
    fScanner            = new RBBIRuleScanner57(this);
    fSetBuilder         = new RBBISetBuilder57(this);
    if (U_FAILURE(status)) {
        return;
    }
    if(fSetBuilder == 0 || fScanner == 0 || fUSetNodes == 0 || fRuleStatusVals == 0) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
}



//----------------------------------------------------------------------------------------
//
//  Destructor
//
//----------------------------------------------------------------------------------------
RBBIRuleBuilder57::~RBBIRuleBuilder57() {

    int        i;
    for (i=0; ; i++) {
        RBBINode *n = (RBBINode *)fUSetNodes->elementAt(i);
        if (n==NULL) {
            break;
        }
        delete n;
    }

    delete fUSetNodes;
    delete fSetBuilder;
    delete fForwardTables;
    delete fReverseTables;
    delete fSafeFwdTables;
    delete fSafeRevTables;

    delete fForwardTree;
    delete fReverseTree;
    delete fSafeFwdTree;
    delete fSafeRevTree;
    delete fScanner;
    delete fRuleStatusVals;
}





//----------------------------------------------------------------------------------------
//
//   flattenData() -  Collect up the compiled RBBI rule data and put it into
//                    the format for saving in ICU data files,
//                    which is also the format needed by the RBBI runtime engine.
//
//----------------------------------------------------------------------------------------
static int32_t align8(int32_t i) {return (i+7) & 0xfffffff8;}

RBBIDataHeader57 *RBBIRuleBuilder57::flattenData() {
    int32_t    i;

    if (U_FAILURE(*fStatus)) {
        return NULL;
    }

    // Remove comments and whitespace from the rules to make it smaller.
    UnicodeString strippedRules((const UnicodeString&)RBBIRuleScanner57::stripRules(fRules));

    // Calculate the size of each section in the data.
    //   Sizes here are padded up to a multiple of 8 for better memory alignment.
    //   Sections sizes actually stored in the header are for the actual data
    //     without the padding.
    //
    int32_t headerSize        = align8(sizeof(RBBIDataHeader57));
    int32_t forwardTableSize  = align8(fForwardTables->getTableSize());
    int32_t reverseTableSize  = align8(fReverseTables->getTableSize());
    int32_t safeFwdTableSize  = align8(fSafeFwdTables->getTableSize());
    int32_t safeRevTableSize  = align8(fSafeRevTables->getTableSize());
    int32_t trieSize          = align8(fSetBuilder->getTrieSize());
    int32_t statusTableSize   = align8(fRuleStatusVals->size() * sizeof(int32_t));
    int32_t rulesSize         = align8((strippedRules.length()+1) * sizeof(UChar));

    int32_t         totalSize = headerSize + forwardTableSize + reverseTableSize
                                + safeFwdTableSize + safeRevTableSize 
                                + statusTableSize + trieSize + rulesSize;

    RBBIDataHeader57  *data     = (RBBIDataHeader57 *)uprv_malloc(totalSize);
    if (data == NULL) {
        *fStatus = U_MEMORY_ALLOCATION_ERROR;
        return NULL;
    }
    uprv_memset(data, 0, totalSize);


    data->fMagic            = 0xb1a0;
    data->fFormatVersion[0] = 3;
    data->fFormatVersion[1] = 1;
    data->fFormatVersion[2] = 0;
    data->fFormatVersion[3] = 0;
    data->fLength           = totalSize;
    data->fCatCount         = fSetBuilder->getNumCharCategories();

    data->fFTable        = headerSize;
    data->fFTableLen     = forwardTableSize;
    data->fRTable        = data->fFTable  + forwardTableSize;
    data->fRTableLen     = reverseTableSize;
    data->fSFTable       = data->fRTable  + reverseTableSize;
    data->fSFTableLen    = safeFwdTableSize;
    data->fSRTable       = data->fSFTable + safeFwdTableSize;
    data->fSRTableLen    = safeRevTableSize;

    data->fTrie          = data->fSRTable + safeRevTableSize;
    data->fTrieLen       = fSetBuilder->getTrieSize();
    data->fStatusTable   = data->fTrie    + trieSize;
    data->fStatusTableLen= statusTableSize;
    data->fRuleSource    = data->fStatusTable + statusTableSize;
    data->fRuleSourceLen = strippedRules.length() * sizeof(UChar);

    uprv_memset(data->fReserved, 0, sizeof(data->fReserved));

    fForwardTables->exportTable((uint8_t *)data + data->fFTable);
    fReverseTables->exportTable((uint8_t *)data + data->fRTable);
    fSafeFwdTables->exportTable((uint8_t *)data + data->fSFTable);
    fSafeRevTables->exportTable((uint8_t *)data + data->fSRTable);
    fSetBuilder->serializeTrie ((uint8_t *)data + data->fTrie);

    int32_t *ruleStatusTable = (int32_t *)((uint8_t *)data + data->fStatusTable);
    for (i=0; i<fRuleStatusVals->size(); i++) {
        ruleStatusTable[i] = fRuleStatusVals->elementAti(i);
    }

    strippedRules.extract((UChar *)((uint8_t *)data+data->fRuleSource), rulesSize/2+1, *fStatus);

    return data;
}






//----------------------------------------------------------------------------------------
//
//  createRuleBasedBreakIterator    construct from source rules that are passed in
//                                  in a UnicodeString
//
//----------------------------------------------------------------------------------------
BreakIterator *
RBBIRuleBuilder57::createRuleBasedBreakIterator( const UnicodeString    &rules,
                                    UParseError      *parseError,
                                    UErrorCode       &status)
{
    // status checked below

    //
    // Read the input rules, generate a parse tree, symbol table,
    // and list of all Unicode Sets referenced by the rules.
    //
    RBBIRuleBuilder57  builder(rules, parseError, status);
    if (U_FAILURE(status)) { // status checked here bcos build below doesn't
        return NULL;
    }
    builder.fScanner->parse();

    //
    // UnicodeSet processing.
    //    Munge the Unicode Sets to create a set of character categories.
    //    Generate the mapping tables (TRIE) from input 32-bit characters to
    //    the character categories.
    //
    builder.fSetBuilder->build();


    //
    //   Generate the DFA state transition table.
    //
    builder.fForwardTables = new RBBITableBuilder57(&builder, &builder.fForwardTree);
    builder.fReverseTables = new RBBITableBuilder57(&builder, &builder.fReverseTree);
    builder.fSafeFwdTables = new RBBITableBuilder57(&builder, &builder.fSafeFwdTree);
    builder.fSafeRevTables = new RBBITableBuilder57(&builder, &builder.fSafeRevTree);
    if (builder.fForwardTables == NULL || builder.fReverseTables == NULL ||
        builder.fSafeFwdTables == NULL || builder.fSafeRevTables == NULL)
    {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete builder.fForwardTables; builder.fForwardTables = NULL;
        delete builder.fReverseTables; builder.fReverseTables = NULL;
        delete builder.fSafeFwdTables; builder.fSafeFwdTables = NULL;
        delete builder.fSafeRevTables; builder.fSafeRevTables = NULL;
        return NULL;
    }

    builder.fForwardTables->build();
    builder.fReverseTables->build();
    builder.fSafeFwdTables->build();
    builder.fSafeRevTables->build();

#ifdef RBBI_DEBUG
    if (builder.fDebugEnv && uprv_strstr(builder.fDebugEnv, "states")) {
        builder.fForwardTables->printRuleStatusTable();
    }
#endif

    //
    //   Package up the compiled data into a memory image
    //      in the run-time format.
    //
    RBBIDataHeader57 *data = builder.flattenData(); // returns NULL if error
    if (U_FAILURE(*builder.fStatus)) {
        return NULL;
    }


    //
    //  Clean up the compiler related stuff
    //


    //
    //  Create a break iterator from the compiled rules.
    //     (Identical to creation from stored pre-compiled rules)
    //
    // status is checked after init in construction.
    RuleBasedBreakIterator57 *This = new RuleBasedBreakIterator57(data, status);
    if (U_FAILURE(status)) {
        delete This;
        This = NULL;
    } 
    else if(This == NULL) { // test for NULL
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return This;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_BREAK_ITERATION */
