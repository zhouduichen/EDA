// Minimal wx stub for compiling the model/netlist logic under g++ on a
// machine without wxWidgets.  Only what netlist.cpp + model.h actually use.
#pragma once
#include <string>
#include <cwchar>
#include <cstdio>
#include <cstdarg>

using wxChar = wchar_t;

class wxString
{
    std::wstring s_;
public:
    wxString() {}
    wxString(const wchar_t* t) : s_(t ? t : L"") {}
    wxString(const std::wstring& t) : s_(t) {}
    explicit wxString(wxChar ch) : s_(1, ch) {}

    wxString& operator+=(const wxString& o) { s_ += o.s_; return *this; }
    wxString& operator+=(const wchar_t* t)  { s_ += t; return *this; }
    wxString& operator+=(wxChar ch)         { s_ += ch; return *this; }
    friend wxString operator+(wxString a, const wxString& b) { a += b; return a; }
    friend wxString operator+(wxString a, const wchar_t* b)  { a += b; return a; }
    friend wxString operator+(const wchar_t* a, const wxString& b) { return wxString(a) + b; }

    bool operator==(const wxString& o) const { return s_ == o.s_; }
    bool operator==(const wchar_t* t) const  { return s_ == t; }
    bool operator!=(const wxString& o) const { return s_ != o.s_; }

    bool    IsEmpty() const { return s_.empty(); }
    bool    empty()   const { return s_.empty(); }
    size_t  size()    const { return s_.size(); }
    wxChar  operator[](size_t i) const { return s_[i]; }
    const wchar_t* c_str() const { return s_.c_str(); }

    static wxString Format(const wxString& fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        wchar_t buf[1024];
        vswprintf(buf, 1024, fmt.c_str(), ap);
        va_end(ap);
        return wxString(buf);
    }

    wxString& Trim(bool fromRight = true)
    {
        const wchar_t* ws = L" \t\r\n";
        if (fromRight)
        {
            while (!s_.empty() && std::wstring(ws).find(s_.back()) != std::wstring::npos)
                s_.pop_back();
        }
        else
        {
            size_t i = 0;
            while (i < s_.size() && std::wstring(ws).find(s_[i]) != std::wstring::npos) ++i;
            s_.erase(0, i);
        }
        return *this;
    }

    bool StartsWith(const wxString& prefix) const
    {
        return s_.compare(0, prefix.s_.size(), prefix.s_) == 0;
    }

    size_t length() const { return s_.length(); }
    void Clear() { s_.clear(); }

    std::string utf8_string() const
    {
        std::string out;
        for (wchar_t ch : s_)
        {
            unsigned int c = (unsigned int)ch;
            if (c < 0x80) out += (char)c;
            else if (c < 0x800)
            {
                out += (char)(0xC0 | (c >> 6));
                out += (char)(0x80 | (c & 0x3F));
            }
            else
            {
                out += (char)(0xE0 | (c >> 12));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        return out;
    }
};

// model.cpp iterates wxString characters and calls wxAtoi.
class wxUniChar
{
    wchar_t ch_;
public:
    wxUniChar(wchar_t c) : ch_(c) {}
    operator wchar_t() const { return ch_; }
};

inline int wxAtoi(const wxString& s)
{
    return (int)wcstol(s.c_str(), nullptr, 10);
}

#define wxT(x) L##x
