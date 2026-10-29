#pragma once
#include "string.h"
#include <vector>

// Simplified tokenizer (single-character separator, no empty-token handling
// differences matter for the test only).
class wxStringTokenizer
{
    std::vector<std::wstring> toks_;
    size_t i_ = 0;
public:
    wxStringTokenizer(const wxString& s, const wxString& sep)
    {
        const wchar_t sc = sep.c_str()[0];
        std::wstring cur;
        for (const wchar_t* q = s.c_str(); ; ++q)
        {
            if (*q == sc || *q == L'\0')
            {
                toks_.push_back(cur);
                cur.clear();
                if (*q == L'\0') break;
            }
            else cur += *q;
        }
    }
    bool HasMoreTokens() const { return i_ < toks_.size(); }
    wxString GetNextToken() { return wxString(toks_[i_++]); }
};
