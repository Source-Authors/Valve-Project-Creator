// Copyright Valve Corporation, All rights reserved.
//
// Serialization buffer.

#include "tier1/utlbuffer.h"

#include <cstdio>
#include <cstdarg>
#include <cctype>
#include <cstdlib>
#include <climits>

#include "tier1/strtools.h"
#include "tier1/characterset.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Character conversions for C strings
//-----------------------------------------------------------------------------
class CUtlCStringConversion : public CUtlCharConversion {
 public:
  CUtlCStringConversion(char nEscapeChar, const char *pDelimiter, intp nCount,
                        ConversionArray_t *pArray);

  // Finds a conversion for the passed-in string, returns length
  virtual char FindConversion(const char *pString, intp *pLength);

 private:
  char m_pConversion[256];
};

//-----------------------------------------------------------------------------
// Character conversions for no-escape sequence strings
//-----------------------------------------------------------------------------
class CUtlNoEscConversion : public CUtlCharConversion {
 public:
  CUtlNoEscConversion(char nEscapeChar, const char *pDelimiter, intp nCount,
                      ConversionArray_t *pArray)
      : CUtlCharConversion(nEscapeChar, pDelimiter, nCount, pArray) {}

  // Finds a conversion for the passed-in string, returns length
  virtual char FindConversion(const char *, intp *pLength) {
    *pLength = 0;
    return 0;
  }
};

//-----------------------------------------------------------------------------
// List of character conversions
//-----------------------------------------------------------------------------
BEGIN_CUSTOM_CHAR_CONVERSION(CUtlCStringConversion, s_StringCharConversion,
                             "\"", '\\'){'\n', "n"},
    {'\t', "t"}, {'\v', "v"}, {'\b', "b"}, {'\r', "r"}, {'\f', "f"},
    {'\a', "a"}, {'\\', "\\"}, {'\?', "\?"}, {'\'', "\'"}, {'\"', "\""},
    END_CUSTOM_CHAR_CONVERSION(CUtlCStringConversion, s_StringCharConversion,
                               "\"", '\\')

        CUtlCharConversion *GetCStringCharConversion() {
  return &s_StringCharConversion;
}

BEGIN_CUSTOM_CHAR_CONVERSION(CUtlNoEscConversion, s_NoEscConversion, "\"",
                             0x7F){0x7F, ""},
    END_CUSTOM_CHAR_CONVERSION(CUtlNoEscConversion, s_NoEscConversion, "\"",
                               0x7F)

        CUtlCharConversion *GetNoEscCharConversion() {
  return &s_NoEscConversion;
}

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CUtlCStringConversion::CUtlCStringConversion(char nEscapeChar,
                                             const char *pDelimiter, intp nCount,
                                             ConversionArray_t *pArray)
    : CUtlCharConversion(nEscapeChar, pDelimiter, nCount, pArray) {
  memset(m_pConversion, 0x0, sizeof(m_pConversion));
  for (intp i = 0; i < nCount; ++i) {
    m_pConversion[(unsigned char)(pArray[i].m_pReplacementString[0])] =
        pArray[i].m_nActualChar;
  }
}

// Finds a conversion for the passed-in string, returns length
char CUtlCStringConversion::FindConversion(const char *pString, intp *pLength) {
  char c = m_pConversion[(unsigned char)(pString[0])];
  *pLength = (c != '\0') ? 1 : 0;
  return c;
}

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CUtlCharConversion::CUtlCharConversion(char nEscapeChar, const char *pDelimiter,
                                       intp nCount, ConversionArray_t *pArray) {
  m_nEscapeChar = nEscapeChar;
  m_pDelimiter = pDelimiter;
  m_nCount = nCount;
  m_nDelimiterLength = V_strlen(pDelimiter);
  m_nMaxConversionLength = 0;

  memset(m_pReplacements, 0, sizeof(m_pReplacements));

  for (intp i = 0; i < nCount; ++i) {
    m_pList[i] = pArray[i].m_nActualChar;
    ConversionInfo_t &info = m_pReplacements[(unsigned char)(m_pList[i])];
    Assert(info.m_pReplacementString == 0);
    info.m_pReplacementString = pArray[i].m_pReplacementString;
    info.m_nLength = V_strlen(info.m_pReplacementString);
    if (info.m_nLength > m_nMaxConversionLength) {
      m_nMaxConversionLength = info.m_nLength;
    }
  }
}

//-----------------------------------------------------------------------------
// Escape character + delimiter
//-----------------------------------------------------------------------------
char CUtlCharConversion::GetEscapeChar() const { return m_nEscapeChar; }

const char *CUtlCharConversion::GetDelimiter() const { return m_pDelimiter; }

intp CUtlCharConversion::GetDelimiterLength() const {
  return m_nDelimiterLength;
}

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
const char *CUtlCharConversion::GetConversionString(char c) const {
  return m_pReplacements[(unsigned char)c].m_pReplacementString;
}

intp CUtlCharConversion::GetConversionLength(char c) const {
  return m_pReplacements[(unsigned char)c].m_nLength;
}

intp CUtlCharConversion::MaxConversionLength() const {
  return m_nMaxConversionLength;
}

//-----------------------------------------------------------------------------
// Finds a conversion for the passed-in string, returns length
//-----------------------------------------------------------------------------
char CUtlCharConversion::FindConversion(const char *pString, intp *pLength) {
  for (intp i = 0; i < m_nCount; ++i) {
    if (!V_strcmp(pString, m_pReplacements[(unsigned char)(m_pList[i])]
                               .m_pReplacementString)) {
      *pLength = m_pReplacements[(unsigned char)(m_pList[i])].m_nLength;
      return m_pList[i];
    }
  }

  *pLength = 0;
  return '\0';
}

//-----------------------------------------------------------------------------
// constructors
//-----------------------------------------------------------------------------
CUtlBuffer::CUtlBuffer(intp growSize, intp initSize, int nFlags) : m_Error(0) {
  MEM_ALLOC_CREDIT();
  m_Memory.Init(growSize, initSize);
  m_Get = 0;
  m_Put = 0;
  m_nTab = 0;
  m_nOffset = 0;
  m_Flags = static_cast<unsigned char>(nFlags);
  m_Reserved = 0;
  if ((initSize != 0) && !IsReadOnly()) {
    m_nMaxPut = -1;
    AddNullTermination(m_Put);
  } else {
    m_nMaxPut = 0;
  }
  SetOverflowFuncs(&CUtlBuffer::GetOverflow, &CUtlBuffer::PutOverflow);
}

CUtlBuffer::CUtlBuffer(const void *pBuffer, intp nSize, int nFlags)
    : m_Memory((unsigned char *)pBuffer, nSize), m_Error(0) {
  Assert(nSize != 0);

  m_Get = 0;
  m_Put = 0;
  m_nTab = 0;
  m_nOffset = 0;
  m_Flags = static_cast<unsigned char>(nFlags);
  m_Reserved = 0;
  if (IsReadOnly()) {
    m_nMaxPut = m_Put = nSize;
  } else {
    m_nMaxPut = -1;
    AddNullTermination(m_Put);
  }
  SetOverflowFuncs(&CUtlBuffer::GetOverflow, &CUtlBuffer::PutOverflow);
}

//-----------------------------------------------------------------------------
// Modifies the buffer to be binary or text; Blows away the buffer and the
// CONTAINS_CRLF value.
//-----------------------------------------------------------------------------
void CUtlBuffer::SetBufferType(bool bIsText, bool bContainsCRLF) {
#ifdef _DEBUG
  // If the buffer is empty, there is no opportunity for this stuff to fail
  if (TellMaxPut() != 0) {
    if (IsText()) {
      if (bIsText) {
        Assert(ContainsCRLF() == bContainsCRLF);
      } else {
        Assert(ContainsCRLF());
      }
    } else {
      if (bIsText) {
        Assert(bContainsCRLF);
      }
    }
  }
#endif

  if (bIsText) {
    m_Flags |= TEXT_BUFFER;
  } else {
    m_Flags &= ~TEXT_BUFFER;
  }
  if (bContainsCRLF) {
    m_Flags |= CONTAINS_CRLF;
  } else {
    m_Flags &= ~CONTAINS_CRLF;
  }
}

//-----------------------------------------------------------------------------
// Attaches the buffer to external memory....
//-----------------------------------------------------------------------------
void CUtlBuffer::SetExternalBuffer(void *pMemory, intp nSize, intp nInitialPut,
                                   int nFlags) {
  m_Memory.SetExternalBuffer((unsigned char *)pMemory, nSize);

  // Reset all indices; we just changed memory
  m_Get = 0;
  m_Put = nInitialPut;
  m_nTab = 0;
  m_Error = 0;
  m_nOffset = 0;
  m_Flags = static_cast<unsigned char>(nFlags);
  m_nMaxPut = -1;
  AddNullTermination(m_Put);
}

//-----------------------------------------------------------------------------
// Assumes an external buffer but manages its deletion
//-----------------------------------------------------------------------------
void CUtlBuffer::AssumeMemory(void *pMemory, intp nSize, intp nInitialPut,
                              int nFlags) {
  m_Memory.AssumeMemory((unsigned char *)pMemory, nSize);

  // Reset all indices; we just changed memory
  m_Get = 0;
  m_Put = nInitialPut;
  m_nTab = 0;
  m_Error = 0;
  m_nOffset = 0;
  m_Flags = static_cast<unsigned char>(nFlags);
  m_nMaxPut = -1;
  AddNullTermination(m_Put);
}

//-----------------------------------------------------------------------------
// Allows the caller to control memory
//-----------------------------------------------------------------------------
void *CUtlBuffer::DetachMemory() {
  // Reset all indices; we just changed memory
  m_Get = 0;
  m_Put = 0;
  m_nTab = 0;
  m_Error = 0;
  m_nOffset = 0;
  return m_Memory.DetachMemory();
}

//-----------------------------------------------------------------------------
// Makes sure we've got at least this much memory
//-----------------------------------------------------------------------------
void CUtlBuffer::EnsureCapacity(intp num) {
  MEM_ALLOC_CREDIT();
  // Add one extra for the null termination
  num += 1;
  if (m_Memory.IsExternallyAllocated()) {
    if (IsGrowable() && (m_Memory.NumAllocated() < num)) {
      m_Memory.ConvertToGrowableMemory(0);
    } else {
      num -= 1;
    }
  }

  m_Memory.EnsureCapacity(num);
}

//-----------------------------------------------------------------------------
// Base get method from which all others derive
//-----------------------------------------------------------------------------
void CUtlBuffer::Get(void *pMem, intp size) {
  if (size > 0 && CheckGet(size)) {
    memcpy(pMem, &m_Memory[m_Get - m_nOffset], size);
    m_Get += size;
  }
}

//-----------------------------------------------------------------------------
// This will get at least 1 byte and up to nSize bytes.
// It will return the number of bytes actually read.
//-----------------------------------------------------------------------------
intp CUtlBuffer::GetUpTo(void *pMem, intp nSize) {
  if (CheckArbitraryPeekGet(0, nSize)) {
    memcpy(pMem, &m_Memory[m_Get - m_nOffset], nSize);
    m_Get += nSize;
    return nSize;
  }
  return 0;
}

//-----------------------------------------------------------------------------
// Eats whitespace
//-----------------------------------------------------------------------------
void CUtlBuffer::EatWhiteSpace() {
  if (IsText() && IsValid()) {
    while (CheckGet(sizeof(char))) {
      if (!V_isspace(*(const unsigned char *)PeekGet())) break;
      m_Get += sizeof(char);
    }
  }
}

//-----------------------------------------------------------------------------
// Eats C++ style comments
//-----------------------------------------------------------------------------
bool CUtlBuffer::EatCPPComment() {
  if (IsText() && IsValid()) {
    // If we don't have a a c++ style comment next, we're done
    const char *pPeek = (const char *)PeekGet(2 * sizeof(char), 0);
    if (!pPeek || (pPeek[0] != '/') || (pPeek[1] != '/')) return false;

    // Deal with c++ style comments
    m_Get += 2;

    // read complete line
    for (char c = GetChar(); IsValid(); c = GetChar()) {
      if (c == '\n') break;
    }
    return true;
  }
  return false;
}

//-----------------------------------------------------------------------------
// Peeks how much whitespace to eat
//-----------------------------------------------------------------------------
intp CUtlBuffer::PeekWhiteSpace(intp nOffset) {
  if (!IsText() || !IsValid()) return 0;

  while (CheckPeekGet(nOffset, sizeof(char))) {
    if (!V_isspace(*(unsigned char *)PeekGet(nOffset))) break;
    nOffset += sizeof(char);
  }

  return nOffset;
}

//-----------------------------------------------------------------------------
// Peek size of sting to come, check memory bound
//-----------------------------------------------------------------------------
intp CUtlBuffer::PeekStringLength() {
  if (!IsValid()) return 0;

  // Eat preceeding whitespace
  intp nOffset = 0;
  if (IsText()) {
    nOffset = PeekWhiteSpace(nOffset);
  }

  intp nStartingOffset = nOffset;

  do {
    intp nPeekAmount = 128;

    // NOTE: Add 1 for the terminating zero!
    if (!CheckArbitraryPeekGet(nOffset, nPeekAmount)) {
      if (nOffset == nStartingOffset) return 0;
      return nOffset - nStartingOffset + 1;
    }

    const char *pTest = (const char *)PeekGet(nOffset);

    if (!IsText()) {
      for (intp i = 0; i < nPeekAmount; ++i) {
        // The +1 here is so we eat the terminating 0
        if (pTest[i] == 0) return (i + nOffset - nStartingOffset + 1);
      }
    } else {
      for (intp i = 0; i < nPeekAmount; ++i) {
        // The +1 here is so we eat the terminating 0
        if (V_isspace((unsigned char)pTest[i]) || (pTest[i] == 0))
          return (i + nOffset - nStartingOffset + 1);
      }
    }

    nOffset += nPeekAmount;

  } while (true);
}

//-----------------------------------------------------------------------------
// Peek size of line to come, check memory bound
//-----------------------------------------------------------------------------
intp CUtlBuffer::PeekLineLength() {
  if (!IsValid()) return 0;

  intp nOffset = 0;
  intp nStartingOffset = nOffset;

  do {
    intp nPeekAmount = 128;

    // NOTE: Add 1 for the terminating zero!
    if (!CheckArbitraryPeekGet(nOffset, nPeekAmount)) {
      if (nOffset == nStartingOffset) return 0;
      return nOffset - nStartingOffset + 1;
    }

    const char *pTest = (const char *)PeekGet(nOffset);

    for (intp i = 0; i < nPeekAmount; ++i) {
      // The +2 here is so we eat the terminating '\n' and 0
      if (pTest[i] == '\n' || pTest[i] == '\r')
        return (i + nOffset - nStartingOffset + 2);
      // The +1 here is so we eat the terminating 0
      if (pTest[i] == 0) return (i + nOffset - nStartingOffset + 1);
    }

    nOffset += nPeekAmount;

  } while (true);
}

//-----------------------------------------------------------------------------
// Does the next bytes of the buffer match a pattern?
//-----------------------------------------------------------------------------
bool CUtlBuffer::PeekStringMatch(intp nOffset, const char *pString, intp nLen) {
  if (!CheckPeekGet(nOffset, nLen)) return false;
  return !V_strncmp((const char *)PeekGet(nOffset), pString, nLen);
}

//-----------------------------------------------------------------------------
// This version of PeekStringLength converts \" to \\ and " to \, etc.
// It also reads a " at the beginning and end of the string
//-----------------------------------------------------------------------------
intp CUtlBuffer::PeekDelimitedStringLength(CUtlCharConversion *pConv,
                                          bool bActualSize) {
  if (!IsText() || !pConv) return PeekStringLength();

  // Eat preceeding whitespace
  intp nOffset = 0;
  if (IsText()) {
    nOffset = PeekWhiteSpace(nOffset);
  }

  if (!PeekStringMatch(nOffset, pConv->GetDelimiter(),
                       pConv->GetDelimiterLength()))
    return 0;

  // Try to read ending ", but don't accept \"
  intp nActualStart = nOffset;
  nOffset += pConv->GetDelimiterLength();
  intp nLen = 1;  // Starts at 1 for the '\0' termination

  do {
    if (PeekStringMatch(nOffset, pConv->GetDelimiter(),
                        pConv->GetDelimiterLength()))
      break;

    if (!CheckPeekGet(nOffset, 1)) break;

    char c = *(const char *)PeekGet(nOffset);
    ++nLen;
    ++nOffset;
    if (c == pConv->GetEscapeChar()) {
      intp nLength = pConv->MaxConversionLength();
      if (!CheckArbitraryPeekGet(nOffset, nLength)) break;

      pConv->FindConversion((const char *)PeekGet(nOffset), &nLength);
      nOffset += nLength;
    }
  } while (true);

  return bActualSize ? nLen
                     : nOffset - nActualStart + pConv->GetDelimiterLength() + 1;
}

//-----------------------------------------------------------------------------
// Reads a null-terminated string
//-----------------------------------------------------------------------------
void CUtlBuffer::GetString(char *pString, intp nMaxChars) {
  if (!IsValid()) {
    *pString = 0;
    return;
  }

  if (nMaxChars == 0) {
    nMaxChars = INT_MAX;
  }

  // Remember, this *includes* the null character
  // It will be 0, however, if the buffer is empty.
  intp nLen = PeekStringLength();

  if (IsText()) {
    EatWhiteSpace();
  }

  if (nLen == 0) {
    *pString = 0;
    m_Error |= GET_OVERFLOW;
    return;
  }

  // Strip off the terminating NULL
  if (nLen <= nMaxChars) {
    Get(pString, nLen - 1);
    pString[nLen - 1] = 0;
  } else {
    Get(pString, nMaxChars - 1);
    pString[nMaxChars - 1] = 0;
    // skip the remaining characters, EXCEPT the terminating null
    // thus it's ( nLen - ( nMaxChars - 1 ) - 1 )
    SeekGet(SEEK_CURRENT, nLen - nMaxChars);
  }

  // Read the terminating NULL in binary formats
  if (!IsText()) {
    VerifyEquals(GetChar(), 0);
  }
}

//-----------------------------------------------------------------------------
// Reads up to and including the first \n
//-----------------------------------------------------------------------------
void CUtlBuffer::GetLine(char *pLine, intp nMaxChars) {
  Assert(IsText() && !ContainsCRLF());

  if (!IsValid()) {
    *pLine = 0;
    return;
  }

  if (nMaxChars == 0) {
    nMaxChars = INT_MAX;
  }

  // Remember, this *includes* the null character
  // It will be 0, however, if the buffer is empty.
  intp nLen = PeekLineLength();
  if (nLen == 0) {
    *pLine = 0;
    m_Error |= GET_OVERFLOW;
    return;
  }

  // Strip off the terminating NULL
  if (nLen <= nMaxChars) {
    Get(pLine, nLen - 1);
    pLine[nLen - 1] = 0;
  } else {
    Get(pLine, nMaxChars - 1);
    pLine[nMaxChars - 1] = 0;
    SeekGet(SEEK_CURRENT, nLen - 1 - nMaxChars);
  }
}

//-----------------------------------------------------------------------------
// This version of GetString converts \ to \\ and " to \", etc.
// It also places " at the beginning and end of the string
//-----------------------------------------------------------------------------
char CUtlBuffer::GetDelimitedCharInternal(CUtlCharConversion *pConv) {
  char c = GetChar();
  if (c == pConv->GetEscapeChar()) {
    intp nLength = pConv->MaxConversionLength();
    if (!CheckArbitraryPeekGet(0, nLength)) return '\0';

    c = pConv->FindConversion((const char *)PeekGet(), &nLength);
    SeekGet(SEEK_CURRENT, nLength);
  }

  return c;
}

char CUtlBuffer::GetDelimitedChar(CUtlCharConversion *pConv) {
  if (!IsText() || !pConv) return GetChar();
  return GetDelimitedCharInternal(pConv);
}

void CUtlBuffer::GetDelimitedString(CUtlCharConversion *pConv, char *pString,
                                    intp nMaxChars) {
  if (!IsText() || !pConv) {
    GetString(pString, nMaxChars);
    return;
  }

  if (!IsValid()) {
    *pString = 0;
    return;
  }

  if (nMaxChars == 0) {
    nMaxChars = INT_MAX;
  }

  EatWhiteSpace();
  if (!PeekStringMatch(0, pConv->GetDelimiter(), pConv->GetDelimiterLength()))
    return;

  // Pull off the starting delimiter
  SeekGet(SEEK_CURRENT, pConv->GetDelimiterLength());

  intp nRead = 0;
  while (IsValid()) {
    if (PeekStringMatch(0, pConv->GetDelimiter(),
                        pConv->GetDelimiterLength())) {
      SeekGet(SEEK_CURRENT, pConv->GetDelimiterLength());
      break;
    }

    char c = GetDelimitedCharInternal(pConv);

    if (nRead < nMaxChars) {
      pString[nRead] = c;
      ++nRead;
    }
  }

  if (nRead >= nMaxChars) {
    nRead = nMaxChars - 1;
  }
  pString[nRead] = '\0';
}

//-----------------------------------------------------------------------------
// Checks if a get is ok
//-----------------------------------------------------------------------------
bool CUtlBuffer::CheckGet(intp nSize) {
  if (m_Error & GET_OVERFLOW) return false;

  if (TellMaxPut() < m_Get + nSize) {
    m_Error |= GET_OVERFLOW;
    return false;
  }

  if ((m_Get < m_nOffset) ||
      (m_Memory.NumAllocated() < m_Get - m_nOffset + nSize)) {
    if (!OnGetOverflow(nSize)) {
      m_Error |= GET_OVERFLOW;
      return false;
    }
  }

  return true;
}

//-----------------------------------------------------------------------------
// Checks if a peek get is ok
//-----------------------------------------------------------------------------
bool CUtlBuffer::CheckPeekGet(intp nOffset, intp nSize) {
  if (m_Error & GET_OVERFLOW) return false;

  // Checking for peek can't set the overflow flag
  bool bOk = CheckGet(nOffset + nSize);
  m_Error &= ~GET_OVERFLOW;
  return bOk;
}

//-----------------------------------------------------------------------------
// Call this to peek arbitrarily long into memory. It doesn't fail unless
// it can't read *anything* new
//-----------------------------------------------------------------------------
bool CUtlBuffer::CheckArbitraryPeekGet(intp nOffset, intp &nIncrement) {
  if (TellGet() + nOffset >= TellMaxPut()) {
    nIncrement = 0;
    return false;
  }

  if (TellGet() + nOffset + nIncrement > TellMaxPut()) {
    nIncrement = TellMaxPut() - TellGet() - nOffset;
  }

  // NOTE: CheckPeekGet could modify TellMaxPut for streaming files
  // We have to call TellMaxPut again here
  CheckPeekGet(nOffset, nIncrement);
  intp nMaxGet = TellMaxPut() - TellGet();
  if (nMaxGet < nIncrement) {
    nIncrement = nMaxGet;
  }
  return (nIncrement != 0);
}

//-----------------------------------------------------------------------------
// Peek part of the butt
//-----------------------------------------------------------------------------
const void *CUtlBuffer::PeekGet(intp nMaxSize, intp nOffset) {
  if (!CheckPeekGet(nOffset, nMaxSize)) return NULL;
  return &m_Memory[m_Get + nOffset - m_nOffset];
}

//-----------------------------------------------------------------------------
// Change where I'm reading
//-----------------------------------------------------------------------------
void CUtlBuffer::SeekGet(SeekType_t type, intp offset) {
  switch (type) {
    case SEEK_HEAD:
      m_Get = offset;
      break;

    case SEEK_CURRENT:
      m_Get += offset;
      break;

    case SEEK_TAIL:
      m_Get = m_nMaxPut - offset;
      break;
  }

  if (m_Get > m_nMaxPut) {
    m_Error |= GET_OVERFLOW;
  } else {
    m_Error &= ~GET_OVERFLOW;
    if (m_Get < m_nOffset || m_Get >= m_nOffset + Size()) {
      OnGetOverflow(-1);
    }
  }
}

//-----------------------------------------------------------------------------
// Parse...
//-----------------------------------------------------------------------------
intp CUtlBuffer::VaScanf(const char *pFmt, va_list list) {
  Assert(pFmt);
  if (m_Error || !IsText()) return 0;

  intp numScanned = 0;
  while (char c = *pFmt++) {
    // Stop if we hit the end of the buffer
    if (m_Get >= TellMaxPut()) {
      m_Error |= GET_OVERFLOW;
      break;
    }

    switch (c) {
      case ' ':
        // eat all whitespace
        EatWhiteSpace();
        break;

      case '%': {
        // Conversion character... try to convert baby!
        char type = *pFmt++;
        if (type == 0) return numScanned;

        switch (type) {
          case 'c': {
            char *ch = va_arg(list, char *);
            if (CheckPeekGet(0, sizeof(char))) {
              *ch = *(const char *)PeekGet();
              ++m_Get;
            } else {
              *ch = 0;
              return numScanned;
            }
          } break;

          case 'h': {
            if (*pFmt == 'd' || *pFmt == 'i') {
              if (!GetTypeText(*va_arg(list, int16 *)))
                return numScanned;  // only support short ints, don't bother
                                    // with hex
            } else if (*pFmt == 'u') {
              if (!GetTypeText(*va_arg(list, uint16 *))) return numScanned;
            } else
              return numScanned;
            ++pFmt;
          } break;

          case 'I': {
            if (*pFmt++ != '6' || *pFmt++ != '4')
              return numScanned;  // only support "I64d" and "I64u"

            if (*pFmt == 'd') {
              if (!GetTypeText(*va_arg(list, int64 *))) return numScanned;
            } else if (*pFmt == 'u') {
              if (!GetTypeText(*va_arg(list, uint64 *))) return numScanned;
            } else {
              return numScanned;
            }

            ++pFmt;
          } break;

          case 'i':
          case 'd': {
            int32 *pArg = va_arg(list, int32 *);
            if (!GetTypeText(*pArg)) return numScanned;
          } break;

          case 'x': {
            uint32 *pArg = va_arg(list, uint32 *);
            if (!GetTypeText(*pArg, 16)) return numScanned;
          } break;

          case 'u': {
            uint32 *pArg = va_arg(list, uint32 *);
            if (!GetTypeText(*pArg)) return numScanned;
          } break;

          case 'l': {
            // we currently support %lf and %lld
            if (*pFmt == 'f') {
              if (!GetTypeText(*va_arg(list, double *))) return numScanned;
            } else if (*pFmt == 'l' && *++pFmt == 'd') {
              if (!GetTypeText(*va_arg(list, int64 *))) return numScanned;
            } else
              return numScanned;
          } break;

          case 'f': {
            float *pArg = va_arg(list, float *);
            if (!GetTypeText(*pArg)) return numScanned;
          } break;

          case 's': {
            char *s = va_arg(list, char *);
            GetString(s);
          } break;

          default: {
            // unimplemented scanf type
            Assert(0);
            return numScanned;
          } break;
        }

        ++numScanned;
      } break;

      default: {
        // Here we have to match the format string character
        // against what's in the buffer or we're done.
        if (!CheckPeekGet(0, sizeof(char))) return numScanned;

        if (c != *(const char *)PeekGet()) return numScanned;

        ++m_Get;
      }
    }
  }
  return numScanned;
}

intp CUtlBuffer::Scanf(SCANF_FORMAT_STRING const char *pFmt, ...) {
  va_list args;

  va_start(args, pFmt);
  intp count = VaScanf(pFmt, args);
  va_end(args);

  return count;
}

//-----------------------------------------------------------------------------
// Advance the get index until after the particular string is found
// Do not eat whitespace before starting. Return false if it failed
//-----------------------------------------------------------------------------
bool CUtlBuffer::GetToken(const char *pToken) {
  Assert(pToken);

  // Look for the token
  intp nLen = V_strlen(pToken);

  // First time through on streaming, check what we already have loaded
  // if we have enough loaded to do the check
  intp nMaxSize = Size() - (TellGet() - m_nOffset);
  if (nMaxSize <= nLen) {
    nMaxSize = Size();
  }
  intp nSizeRemaining = TellMaxPut() - TellGet();

  intp nGet = TellGet();
  while (nSizeRemaining >= nLen) {
    bool bOverFlow = (nSizeRemaining > nMaxSize);
    intp nSizeToCheck = bOverFlow ? nMaxSize : nSizeRemaining;
    if (!CheckPeekGet(0, nSizeToCheck)) break;

    const char *pBufStart = (const char *)PeekGet();
    const char *pFoundEnd = V_strnistr(pBufStart, pToken, nSizeToCheck);

    // Time to be careful: if we are in a state of overflow
    // (namely, there's more of the buffer beyond the current window)
    // we could be looking for 'foo' for example, and find 'foobar'
    // if 'foo' happens to be the last 3 characters of the current window
    size_t nOffset = (size_t)pFoundEnd - (size_t)pBufStart;
    bool bPotentialMismatch = (bOverFlow && ((intp)nOffset == Size() - nLen));
    if (!pFoundEnd || bPotentialMismatch) {
      nSizeRemaining -= nSizeToCheck;
      if (!pFoundEnd && (nSizeRemaining < nLen)) break;

      // Second time through, stream as much in as possible
      // But keep the last portion of the current buffer
      // since we couldn't check it against stuff outside the window
      nSizeRemaining += nLen;
      nMaxSize = Size();
      SeekGet(CUtlBuffer::SEEK_CURRENT, nSizeToCheck - nLen);
      continue;
    }

    // Seek past the end of the found string
    SeekGet(CUtlBuffer::SEEK_CURRENT, (nOffset + nLen));
    return true;
  }

  // Didn't find a match, leave the get index where it was to start with
  SeekGet(CUtlBuffer::SEEK_HEAD, nGet);
  return false;
}

//-----------------------------------------------------------------------------
// (For text buffers only)
// Parse a token from the buffer:
// Grab all text that lies between a starting delimiter + ending delimiter
// (skipping whitespace that leads + trails both delimiters).
// Note the delimiter checks are case-insensitive.
// If successful, the get index is advanced and the function returns true,
// otherwise the index is not advanced and the function returns false.
//-----------------------------------------------------------------------------
bool CUtlBuffer::ParseToken(const char *pStartingDelim,
                            const char *pEndingDelim, char *pString,
                            intp nMaxLen) {
  intp nCharsToCopy = 0;
  intp nCurrentGet = 0;

  size_t nEndingDelimLen;

  // Starting delimiter is optional
  char emptyBuf = '\0';
  if (!pStartingDelim) {
    pStartingDelim = &emptyBuf;
  }

  // Ending delimiter is not
  Assert(pEndingDelim && pEndingDelim[0]);
  nEndingDelimLen = V_strlen(pEndingDelim);

  intp nStartGet = TellGet();
  char nCurrChar;
  intp nTokenStart = -1;
  EatWhiteSpace();
  while (*pStartingDelim) {
    nCurrChar = *pStartingDelim++;
    if (!V_isspace((unsigned char)nCurrChar)) {
      if (tolower(GetChar()) != tolower(nCurrChar)) goto parseFailed;
    } else {
      EatWhiteSpace();
    }
  }

  EatWhiteSpace();
  nTokenStart = TellGet();
  if (!GetToken(pEndingDelim)) goto parseFailed;

  nCurrentGet = TellGet();
  nCharsToCopy = (intp)((nCurrentGet - nEndingDelimLen) - nTokenStart);
  if (nCharsToCopy >= nMaxLen) {
    nCharsToCopy = nMaxLen - 1;
  }

  if (nCharsToCopy > 0) {
    SeekGet(CUtlBuffer::SEEK_HEAD, nTokenStart);
    Get(pString, nCharsToCopy);
    if (!IsValid()) goto parseFailed;

    // Eat trailing whitespace
    for (; nCharsToCopy > 0; --nCharsToCopy) {
      if (!V_isspace((unsigned char)pString[nCharsToCopy - 1])) break;
    }
  }
  pString[nCharsToCopy] = '\0';

  // Advance the Get index
  SeekGet(CUtlBuffer::SEEK_HEAD, nCurrentGet);
  return true;

parseFailed:
  // Revert the get index
  SeekGet(SEEK_HEAD, nStartGet);
  pString[0] = '\0';
  return false;
}

//-----------------------------------------------------------------------------
// Parses the next token, given a set of character breaks to stop at
//-----------------------------------------------------------------------------
intp CUtlBuffer::ParseToken(characterset_t *pBreaks, char *pTokenBuf,
                            intp nMaxLen, bool bParseComments) {
  Assert(nMaxLen > 0);
  pTokenBuf[0] = 0;

  // skip whitespace + comments
  while (true) {
    if (!IsValid()) return -1;
    EatWhiteSpace();
    if (bParseComments) {
      if (!EatCPPComment()) break;
    } else {
      break;
    }
  }

  char c = GetChar();

  // End of buffer
  if (c == 0) return -1;

  // handle quoted strings specially
  if (c == '\"') {
    intp nLen = 0;
    while (IsValid()) {
      c = GetChar();
      if (c == '\"' || !c) {
        pTokenBuf[nLen] = 0;
        return nLen;
      }
      pTokenBuf[nLen] = c;
      if (++nLen == nMaxLen) {
        pTokenBuf[nLen - 1] = 0;
        return nMaxLen;
      }
    }

    // In this case, we hit the end of the buffer before hitting the end qoute
    pTokenBuf[nLen] = 0;
    return nLen;
  }

  // parse single characters
  if (IN_CHARACTERSET(*pBreaks, c)) {
    pTokenBuf[0] = c;
    pTokenBuf[1] = 0;
    return 1;
  }

  // parse a regular word
  intp nLen = 0;
  while (true) {
    pTokenBuf[nLen] = c;
    if (++nLen == nMaxLen) {
      pTokenBuf[nLen - 1] = 0;
      return nMaxLen;
    }
    c = GetChar();
    if (!IsValid()) break;

    if (IN_CHARACTERSET(*pBreaks, c) || c == '\"' || c <= ' ') {
      SeekGet(SEEK_CURRENT, -1);
      break;
    }
  }

  pTokenBuf[nLen] = 0;
  return nLen;
}

//-----------------------------------------------------------------------------
// Serialization
//-----------------------------------------------------------------------------
void CUtlBuffer::Put(const void *pMem, intp size) {
  if (size && CheckPut(size)) {
    memcpy(&m_Memory[m_Put - m_nOffset], pMem, size);
    m_Put += size;

    AddNullTermination(m_Put);
  }
}

//-----------------------------------------------------------------------------
// Writes a null-terminated string
//-----------------------------------------------------------------------------
void CUtlBuffer::PutString(const char *pString) {
  if (!IsText()) {
    if (pString) {
      // Not text? append a null at the end.
      intp nLen = V_strlen(pString) + 1;
      Put(pString, nLen * sizeof(char));
      return;
    } else {
      PutTypeBin<char>(0);
    }
  } else if (pString) {
    intp nTabCount = (m_Flags & AUTO_TABS_DISABLED) ? 0 : m_nTab;
    if (nTabCount > 0) {
      if (WasLastCharacterCR()) {
        PutTabs();
      }

      const char *pEndl = strchr(pString, '\n');
      while (pEndl) {
        size_t nSize = (size_t)pEndl - (size_t)pString + sizeof(char);
        Put(pString, (intp)nSize);
        pString = pEndl + 1;
        if (*pString) {
          PutTabs();
          pEndl = strchr(pString, '\n');
        } else {
          pEndl = NULL;
        }
      }
    }
    intp nLen = V_strlen(pString);
    if (nLen) {
      Put(pString, nLen * sizeof(char));
    }
  }
}

//-----------------------------------------------------------------------------
// This version of PutString converts \ to \\ and " to \", etc.
// It also places " at the beginning and end of the string
//-----------------------------------------------------------------------------
inline void CUtlBuffer::PutDelimitedCharInternal(CUtlCharConversion *pConv,
                                                 char c) {
  intp l = pConv->GetConversionLength(c);
  if (l == 0) {
    PutChar(c);
  } else {
    PutChar(pConv->GetEscapeChar());
    Put(pConv->GetConversionString(c), l);
  }
}

void CUtlBuffer::PutDelimitedChar(CUtlCharConversion *pConv, char c) {
  if (!IsText() || !pConv) {
    PutChar(c);
    return;
  }

  PutDelimitedCharInternal(pConv, c);
}

void CUtlBuffer::PutDelimitedString(CUtlCharConversion *pConv,
                                    const char *pString) {
  if (!IsText() || !pConv) {
    PutString(pString);
    return;
  }

  if (WasLastCharacterCR()) {
    PutTabs();
  }
  Put(pConv->GetDelimiter(), pConv->GetDelimiterLength());

  intp nLen = pString ? V_strlen(pString) : 0;
  for (intp i = 0; i < nLen; ++i) {
    PutDelimitedCharInternal(pConv, pString[i]);
  }

  if (WasLastCharacterCR()) {
    PutTabs();
  }
  Put(pConv->GetDelimiter(), pConv->GetDelimiterLength());
}

void CUtlBuffer::VaPrintf(const char *pFmt, va_list list) {
  char temp[8192];
  int nLen = V_vsnprintf(temp, sizeof(temp), pFmt, list);
  ErrorIfNot(
      nLen < static_cast<intp>(sizeof(temp)),
      ("CUtlBuffer::VaPrintf: String overflowed buffer [%zu]\n", sizeof(temp)));
  PutString(temp);
}

void CUtlBuffer::Printf(const char *pFmt, ...) {
  va_list args;

  va_start(args, pFmt);
  VaPrintf(pFmt, args);
  va_end(args);
}

//-----------------------------------------------------------------------------
// Calls the overflow functions
//-----------------------------------------------------------------------------
void CUtlBuffer::SetOverflowFuncs(UtlBufferOverflowFunc_t getFunc,
                                  UtlBufferOverflowFunc_t putFunc) {
  m_GetOverflowFunc = getFunc;
  m_PutOverflowFunc = putFunc;
}

//-----------------------------------------------------------------------------
// Calls the overflow functions
//-----------------------------------------------------------------------------
bool CUtlBuffer::OnPutOverflow(intp nSize) {
  return (this->*m_PutOverflowFunc)(nSize);
}

bool CUtlBuffer::OnGetOverflow(intp nSize) {
  return (this->*m_GetOverflowFunc)(nSize);
}

//-----------------------------------------------------------------------------
// Checks if a put is ok
//-----------------------------------------------------------------------------
bool CUtlBuffer::PutOverflow(intp nSize) {
  MEM_ALLOC_CREDIT();

  if (m_Memory.IsExternallyAllocated()) {
    if (!IsGrowable()) return false;

    m_Memory.ConvertToGrowableMemory(0);
  }

  while (Size() < m_Put - m_nOffset + nSize) {
    m_Memory.Grow();
  }

  return true;
}

bool CUtlBuffer::GetOverflow(intp) { return false; }

//-----------------------------------------------------------------------------
// Checks if a put is ok
//-----------------------------------------------------------------------------
bool CUtlBuffer::CheckPut(intp nSize) {
  if ((m_Error & PUT_OVERFLOW) || IsReadOnly()) return false;

  if ((m_Put < m_nOffset) ||
      (m_Memory.NumAllocated() < m_Put - m_nOffset + nSize)) {
    if (!OnPutOverflow(nSize)) {
      m_Error |= PUT_OVERFLOW;
      return false;
    }
  }
  return true;
}

void CUtlBuffer::SeekPut(SeekType_t type, intp offset) {
  intp nNextPut = m_Put;
  switch (type) {
    case SEEK_HEAD:
      nNextPut = offset;
      break;

    case SEEK_CURRENT:
      nNextPut += offset;
      break;

    case SEEK_TAIL:
      nNextPut = m_nMaxPut - offset;
      break;
  }

  // Force a write of the data
  // FIXME: We could make this more optimal potentially by writing out
  // the entire buffer if you seek outside the current range

  // NOTE: This call will write and will also seek the file to nNextPut.
  OnPutOverflow(-nNextPut - 1);
  m_Put = nNextPut;

  AddNullTermination(m_Put);
}

void CUtlBuffer::ActivateByteSwapping(bool bActivate) {
  m_Byteswap.ActivateByteSwapping(bActivate);
}

void CUtlBuffer::SetBigEndian(bool bigEndian) {
  m_Byteswap.SetTargetBigEndian(bigEndian);
}

bool CUtlBuffer::IsBigEndian(void) { return m_Byteswap.IsTargetBigEndian(); }

//-----------------------------------------------------------------------------
// null terminate the buffer
// NOTE: Pass in nPut here even though it is just a copy of m_Put.  This is
// almost always called immediately after modifying m_Put and this lets it stay
// in a register and avoid LHS on PPC.
//-----------------------------------------------------------------------------
void CUtlBuffer::AddNullTermination(intp nPut) {
  if (nPut > m_nMaxPut) {
    if (!IsReadOnly() && ((m_Error & PUT_OVERFLOW) == 0)) {
      // Add null termination value
      if (CheckPut(1)) {
        m_Memory[nPut - m_nOffset] = 0;
      } else {
        // Restore the overflow state, it was valid before...
        m_Error &= ~PUT_OVERFLOW;
      }
    }
    m_nMaxPut = nPut;
  }
}

//-----------------------------------------------------------------------------
// Converts a buffer from a CRLF buffer to a CR buffer (and back)
// Returns false if no conversion was necessary (and outBuf is left untouched)
// If the conversion occurs, outBuf will be cleared.
//-----------------------------------------------------------------------------
bool CUtlBuffer::ConvertCRLF(CUtlBuffer &outBuf) {
  if (!IsText() || !outBuf.IsText()) return false;

  if (ContainsCRLF() == outBuf.ContainsCRLF()) return false;

  intp nInCount = TellMaxPut();

  outBuf.Purge();
  outBuf.EnsureCapacity(nInCount);

  bool bFromCRLF = ContainsCRLF();

  // Start reading from the beginning
  intp nGet = TellGet();
  intp nPut = TellPut();
  intp nGetDelta = 0;
  intp nPutDelta = 0;

  const char *pBase = (const char *)Base();
  intp nCurrGet = 0;
  while (nCurrGet < nInCount) {
    const char *pCurr = &pBase[nCurrGet];
    if (bFromCRLF) {
      const char *pNext = V_strnistr(pCurr, "\r\n", nInCount - nCurrGet);
      if (!pNext) {
        outBuf.Put(pCurr, nInCount - nCurrGet);
        break;
      }

      intp nBytes = pNext - pCurr;
      outBuf.Put(pCurr, nBytes);
      outBuf.PutChar('\n');
      nCurrGet += nBytes + 2;
      if (nGet >= nCurrGet - 1) {
        --nGetDelta;
      }
      if (nPut >= nCurrGet - 1) {
        --nPutDelta;
      }
    } else {
      const char *pNext = V_strnchr(pCurr, '\n', nInCount - nCurrGet);
      if (!pNext) {
        outBuf.Put(pCurr, nInCount - nCurrGet);
        break;
      }

      intp nBytes = pNext - pCurr;
      outBuf.Put(pCurr, nBytes);
      outBuf.PutChar('\r');
      outBuf.PutChar('\n');
      nCurrGet += nBytes + 1;
      if (nGet >= nCurrGet) {
        ++nGetDelta;
      }
      if (nPut >= nCurrGet) {
        ++nPutDelta;
      }
    }
  }

  Assert(nPut + nPutDelta <= outBuf.TellMaxPut());

  outBuf.SeekGet(SEEK_HEAD, nGet + nGetDelta);
  outBuf.SeekPut(SEEK_HEAD, nPut + nPutDelta);

  return true;
}

//---------------------------------------------------------------------------
// Implementation of CUtlInplaceBuffer
//---------------------------------------------------------------------------

CUtlInplaceBuffer::CUtlInplaceBuffer(intp growSize /* = 0 */,
                                     intp initSize /* = 0 */,
                                     int nFlags /* = 0 */)
    : CUtlBuffer(growSize, initSize, nFlags) {
}

bool CUtlInplaceBuffer::InplaceGetLinePtr(char **ppszInBufferPtr,
                                          intp *pnLineLength) {
  Assert(IsText() && !ContainsCRLF());

  intp nLineLen = PeekLineLength();
  if (nLineLen <= 1) {
    SeekGet(SEEK_TAIL, 0);
    return false;
  }

  --nLineLen;  // because it accounts for putting a terminating null-character

  char *pszLine = (char *)const_cast<void *>(PeekGet());
  SeekGet(SEEK_CURRENT, nLineLen);

  // Set the out args
  if (ppszInBufferPtr) *ppszInBufferPtr = pszLine;

  if (pnLineLength) *pnLineLength = nLineLen;

  return true;
}

char *CUtlInplaceBuffer::InplaceGetLinePtr(void) {
  char *pszLine = NULL;
  intp nLineLen = 0;

  if (InplaceGetLinePtr(&pszLine, &nLineLen)) {
    Assert(nLineLen >= 1);

    switch (pszLine[nLineLen - 1]) {
      case '\n':
      case '\r':
        pszLine[nLineLen - 1] = 0;
        if (--nLineLen) {
          switch (pszLine[nLineLen - 1]) {
            case '\n':
            case '\r':
              pszLine[nLineLen - 1] = 0;
              break;
          }
        }
        break;

      default:
        Assert(pszLine[nLineLen] == 0);
        break;
    }
  }

  return pszLine;
}
