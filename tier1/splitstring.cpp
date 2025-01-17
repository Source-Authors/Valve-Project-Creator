// Copyright Valve Corporation. All Rights Reserved.

#include "tier1/strtools.h"

#include "tier1/utlvector.h"

#include "tier0/memdbgon.h"

CSplitString::CSplitString(const char *pString, const char **pSeparators,
                           intp nSeparators) {
  Construct(pString, pSeparators, nSeparators);
};

CSplitString::CSplitString(const char *pString, const char *pSeparator) {
  Construct(pString, &pSeparator, 1);
}

CSplitString::~CSplitString() {
  if (m_szBuffer) delete[] m_szBuffer;
}

void CSplitString::Construct(const char *pString, const char **pSeparators,
                             intp nSeparators) {
  //////////////////////////////////////////////////////////////////////////
  // make a duplicate of the original string. We'll use pieces of this duplicate
  // to tokenize the string and create NULL-terminated tokens of the original
  // string
  //
  intp nOriginalStringLength = V_strlen(pString);
  m_szBuffer = new char[nOriginalStringLength + 1];
  memcpy(m_szBuffer, pString, nOriginalStringLength + 1);

  this->Purge();
  const char *pCurPos = pString;
  while (1) {
    intp iFirstSeparator = -1;
    const char *pFirstSeparator = 0;
    for (intp i = 0; i < nSeparators; i++) {
      const char *pTest = V_stristr(pCurPos, pSeparators[i]);
      if (pTest && (!pFirstSeparator || pTest < pFirstSeparator)) {
        iFirstSeparator = i;
        pFirstSeparator = pTest;
      }
    }

    if (pFirstSeparator) {
      // Split on this separator and continue on.
      intp separatorLen = V_strlen(pSeparators[iFirstSeparator]);
      if (pFirstSeparator > pCurPos) {
        //////////////////////////////////////////////////////////////////////////
        /// Cut the token out of the duplicate string
        char *pTokenInDuplicate = m_szBuffer + (pCurPos - pString);
        intp nTokenLength = pFirstSeparator - pCurPos;
        Assert(nTokenLength > 0 &&
               !memcmp(pTokenInDuplicate, pCurPos, nTokenLength));
        pTokenInDuplicate[nTokenLength] = '\0';

        this->AddToTail( pTokenInDuplicate /*AllocString( pCurPos, pFirstSeparator-pCurPos )*/ );
      }

      pCurPos = pFirstSeparator + separatorLen;
    } else {
      // Copy the rest of the string
      if ([[maybe_unused]] intp nTokenLength = V_strlen(pCurPos)) {
        //////////////////////////////////////////////////////////////////////////
        // There's no need to cut this token, because there's no separator after
        // it. just add its copy in the buffer to the tail
        char *pTokenInDuplicate = m_szBuffer + (pCurPos - pString);
        Assert(!memcmp(pTokenInDuplicate, pCurPos, nTokenLength));

        this->AddToTail(pTokenInDuplicate /*AllocString( pCurPos, -1 )*/);
      }
      return;
    }
  }
}

void CSplitString::PurgeAndDeleteElements() { Purge(); }
