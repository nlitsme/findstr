#include "util/rw/MmapReader.h"

#ifdef USE_BOOST_REGEX
#include <boost/regex.hpp>
#define BASIC_REGEX boost::basic_regex
#define REGEX_ITER  boost::regex_iterator
#define REGEX_MATCH boost::regex_match
#define PARTIALARG  , boost::match_partial
#endif

#ifdef USE_STD_REGEX
#include <regex>
#define BASIC_REGEX std::basic_regex
#define REGEX_ITER  std::regex_iterator
#define REGEX_MATCH std::regex_match
#define PARTIALARG
#endif

#include <boost/format.hpp>
#define FORMATTER boost::format

#include "FileFunctions.h"
#include "stringutils.h"
#include "args.h"
#include <set>

// todo: why does  basic_regex<uint8_t>  throw a 'bad_cast' exception?

struct findstr {
    bool matchword;      // modifies pattern
    bool matchbinary;    // modifies pattern, modifies verbose output
    bool matchcase;      // modifies pattern
    bool pattern_is_hex; // modifies verbose output, implies binary
    bool pattern_is_guid;// modifies verbose output, implies binary
    bool verbose;        // modifies ouput
    bool list_only;      // modifies ouput
    bool count_only;     // modifies ouput
    bool readcontinuous; // read until ctrl-c, instead of until eof
    uint64_t maxfilesize;

    std::string pattern;
    std::string unicodepattern;

    findstr() :
        matchword(false),
        matchbinary(false),
        matchcase(false),
        pattern_is_hex(false),
        pattern_is_guid(false),
        verbose(false),
        list_only(false),
        count_only(false),
        readcontinuous(false),
        maxfilesize(0)
    {
    }

    // returns:
    //     NULL   when final match found
    //     last   when only complete matches were found
    //     *      when partial match was found
    template<typename CB>
    const char *searchrange(const BASIC_REGEX<char>& re, const char *first, const char *last, CB cb)
    {
        REGEX_ITER<const char*> a(first, last, re   PARTIALARG);
        REGEX_ITER<const char*> b;

        const char *maxpartial= NULL;
        const char *maxmatch= NULL;

        //printf("searchrange(%p, %p)\n", first, last);
        while (a!=b) {
            auto m= (*a)[0];
            //printf("    match %d  %p..%p\n", m.matched, m.first, m.second);
            if (m.matched) {
                if (!cb(m.first, m.second)) {
                    //printf("searchrange: stopping\n");
                    return NULL;
                }
                if (maxmatch==NULL || maxmatch<m.first)
                    maxmatch= m.first;
            }
            else {
                if (maxpartial==NULL || maxpartial<m.first)
                    maxpartial= m.first;
            }

            ++a;
        }
        if ((maxmatch==NULL && maxpartial==NULL) || maxmatch>maxpartial) {
            //printf("searchrange: no partial match\n");
            return last;
        }

        //printf("searchrange: partial match @%lx\n", maxpartial-first);
        return maxpartial;
    }

    void searchstdin()
    {
        //printf("searching stdin\n");
        // see: http://www.boost.org/doc/libs/1_52_0/libs/regex/doc/html/boost_regex/partial_matches.html

        bool nameprinted= false;
        int matchcount= 0;
        std::vector<char> buf(256);
        char *bufstart= &buf.front();
        char *bufend= bufstart+buf.size();
        uint64_t offset= 0;

        char *readptr= bufstart;

        BASIC_REGEX<char> re(pattern.c_str(), pattern.c_str()+pattern.size());

        while (true)
        {
            size_t needed= bufend-readptr;
            //printf("%08llx: needed= %zd\n", offset, needed);
            int n= read(0, readptr, needed);
            if (n==0) {
                if (readcontinuous) {
                    //printf("stdin: waiting for more\n");
                    usleep(100);
                    continue;
                }
                break;
            }
            else if (n>needed || n<0)
                break;

            char *readend= readptr+n;

            const char *partial= searchrange(re, bufstart, readend, [bufstart, offset, &nameprinted, &matchcount, this](const char *first, const char *last)->bool {
                matchcount++;
                if (count_only)
                    return true;
                if (list_only) {
                    std::cout << "-\n" << std::endl;
                    return false;
                }
                else if (verbose) {
                    if (pattern_is_hex || matchbinary)
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % hexdump((const uint8_t*)first, last-first);
                    else if (pattern_is_guid)
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % guidstring((const uint8_t*)first);
                    else
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % ascdump((const uint8_t*)first, last-first);
                }
                else {
                    if (!nameprinted) {
                        std::cout << "-" << std::endl;
                        std::cout << "\t";
                    }
                    else {
                        std::cout << ", ";
                    }
                    std::cout << boost::format("%08x") % (offset+first-bufstart);
                    nameprinted= true;
                }
                return true;
            });

            // avoid too large partial matches
            if (partial-bufstart < buf.size()/2)
                partial=bufstart+buf.size()/2;

            // relocate data for partial matches
            if (partial<readend) {
                memcpy(bufstart, partial, readend-partial);
                readptr= bufstart+(readend-partial);
                n -= (readend-partial);
            }
            else {
                readptr= bufstart;
            }

            offset += n;
        }
        if (count_only)
            std::cout << boost::format("%6d %s\n") % matchcount % "-";
    }
    static boost::format guidstring(const uint8_t *p)
    {
        return boost::format("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x")
            % get32le(p+0)
            % get16le(p+4)
            % get16le(p+6)
            % get16be(p+8)
            % get8(p+10) % get8(p+11) % get8(p+12) % get8(p+13) % get8(p+14) % get8(p+15);
    }
    void searchfile(const std::string& fn)
    {
        uint64_t filesize= GetFileSize(fn);
        if (filesize==0)
            return;
        if (maxfilesize && filesize>=maxfilesize) {
            if (verbose)
                std::cout << "skipping large file " << fn << std::endl;
            return;
        }

        //printf("searching %s\n", fn.c_str());
        bool nameprinted= false;
        int matchcount= 0;
        MmapReader r(fn, MmapReader::readonly);
        const char*filefirst= (const char*)r.begin();
        const char*filelast= (const char*)r.end();

        BASIC_REGEX<char> re(pattern.c_str(), pattern.c_str()+pattern.size());

        searchrange(re, filefirst, filelast, [&fn, filefirst, &nameprinted, &matchcount, this](const char *first, const char *last)->bool {
            matchcount++;
            if (count_only)
                return true;
            if (list_only) {
                std::cout << fn << std::endl;
                return false;
            }
            else if (verbose) {
                if (pattern_is_hex || matchbinary)
                    std::cout << boost::format("%s %08x %s\n") % fn % (first-filefirst) % hexdump((const uint8_t*)first, last-first);
                else if (pattern_is_guid)                                                   
                    std::cout << boost::format("%s %08x %s\n") % fn % (first-filefirst) % guidstring((const uint8_t*)first);
                else                                                                   
                    std::cout << boost::format("%s %08x %s\n") % fn % (first-filefirst) % ascdump((const uint8_t*)first, last-first);
            }
            else {
                if (!nameprinted) {
                    std::cout << fn << std::endl;
                    std::cout << "\t";
                }
                else {
                    std::cout << ", ";
                }
                std::cout << boost::format("%08x") % (first-filefirst);
                nameprinted= true;
            }
            return true;
        });

        if (count_only)
            std::cout << boost::format("%6d %s\n") % matchcount % fn;
        if (nameprinted)
            std::cout << std::endl;
    }
    bool compile_pattern()
    {
        if (pattern_is_hex) {
            return compile_hex_pattern();
        }
        else if (pattern_is_guid) {
            return compile_guid_pattern();
        }
        else if (!matchbinary) {
            pattern += "|" + translate_pattern(pattern);
        }
        return true;
    }
    bool compile_hex_pattern()
    {
        std::set<int> sizes;

        auto i= pattern.begin();
        while (i!=pattern.end()) {
            auto j= std::find_if(i, pattern.end(), isxdigit);
            if (j==pattern.end())
                break;
            i= std::find_if(j, pattern.end(), [](char c){ return !isxdigit(c); });
            sizes.insert(i-j);
        }
        ByteVector data;

        if (sizes.size()==1) {
            size_t nyblecount= *sizes.begin();
            switch(nyblecount)
            {
                case 2: hex2binary(pattern, data);
                        break;
                case 4: {
                            std::vector<uint16_t> v;
                            hex2binary(pattern, v);
                            data.resize(v.size()*sizeof(uint16_t));
                            for(int i=0 ; i<v.size() ; i++)
                                set16le(&data[sizeof(uint16_t)*i], v[i]);
                        }
                        break;
                case 8: {
                            std::vector<uint32_t> v;
                            hex2binary(pattern, v);
                            data.resize(v.size()*sizeof(uint32_t));
                            for(int i=0 ; i<v.size() ; i++)
                                set32le(&data[sizeof(uint32_t)*i], v[i]);
                        }
                        break;
                case 16:{
                            std::vector<uint64_t> v;
                            hex2binary(pattern, v);
                            data.resize(v.size()*sizeof(uint64_t));
                            for(int i=0 ; i<v.size() ; i++)
                                set64le(&data[sizeof(uint64_t)*i], v[i]);
                        }
                        break;
                default: hex2binary(pattern, data);
                        break;
            }
        }
        else {
            hex2binary(pattern, data);
        }
        pattern.clear();
        for (auto c : data)
            pattern += stringformat("\\x%02x", c);
        return true;
    }
    bool compile_guid_pattern()
    {
        ByteVector guid(16);
        // wwwwwwww-xxxx-xxxx-bbbb-bbbbbbbbbbbb
        
        int i= 0;
        auto p= &pattern[0];
        auto pend= p+pattern.size();
        while (p < pend) {
            switch(i) {
                case 0:
                    {
                    auto q= p;
                    uint32_t l1= strtoul(p, &q, 16);
                    if (*q!='-')
                        return false;
                    q++;
                    set32le(&guid[i], l1);
                    i += 4;
                    }
                    break;
                case 4:
                case 6:
                    {
                    auto q= p;
                    uint16_t w= strtoul(p, &q, 16);
                    if (*q!='-')
                        return false;
                    q++;
                    set32le(&guid[i], w);
                    i += 2;
                    }
                    break;
                default:
                    {
                    size_t n= hex2binary(p, pend, &guid[i], &guid[0]+16);
                    if (n!=8)
                        return false;
                    p= pend;
                    i += n;
                    }
            }

        }
        return true;
    }
    std::string translate_pattern(std::string& apat)
    {
        std::string upat;
        // translate [...] -> [...]\x00
        // translate (...) { * | + | ? | {\d*,\d*} } \??  -> (...) QUANT
        // normal  : not { * | + | ? | . | { | ( | ) | ^ | $ | [ | ] | \ }   -> .\x00
        // \xXX    -> \xXX\x00
        // (?[#:=!>]....)

        std::string esc;
        std::string charset;
        std::string quantifier;

        for (auto c : apat)
        {
            if (!esc.empty()) {
                esc += c;
                if (esc.size()>1) {
                    if (esc[1]!='x' || esc.size()==4) {
                        upat += esc;
                        upat += "\\x00";
                        esc.clear();
                    }
                }
            }
            else if (c=='\\') {
                esc += c;
            }
            else if (!quantifier.empty()) {
                quantifier += c;
                if (c=='}') {
                    upat += quantifier;
                    quantifier.clear();
                }
            }
            else if (!charset.empty()) {
                charset += c;
                if (c==']') {
                    upat += charset;
                    upat += "\\x00";
                    charset.clear();
                }
            }
            else if (c=='[') {
                charset += c;
            }
            else if (c=='{') {
                quantifier += c;
            }
            else if (c!='(' && c!=')' && c!='*' && c!='+' && c!='?' && c!='^' && c!='$') {
                upat += c;
                upat += "\\x00";
            }
            else {
                upat += c;
            }
        }
        return upat;
    }
};
struct tokenize {
    std::string str;
    char sep;
    struct token {
        std::string::const_iterator i;
        std::string::const_iterator last;
        char sep;
        token(
            std::string::const_iterator i,
            std::string::const_iterator last,
            char sep)
            : i(i), last(last), sep(sep)
        {
        }
        std::string operator*() const
        {
            auto isep= std::find(i, last, sep);
            return std::string(i, isep);
        }
        token& operator++()
        {
            ++i;
            i= std::find(i, last, sep);

            return *this;
        }
        token operator++(int)
        {
            token copy= *this;
            operator++();
            return copy;
        }
        bool operator!=(const token& rhs) const
        {
            return i!=rhs.i;
        }
    };
    tokenize(const std::string& str, char sep)
        : str(str), sep(sep)
    {
    }
    token begin() const
    {
        return token(str.begin(), str.end(), sep);
    }
    token end() const
    {
        return token(str.end(), str.end(), sep);
    }
};
int main(int argc, char**argv)
{
    bool recurse_dirs= false;
    StringList args;
    findstr  f;
    std::string excludepaths;

    for (int i=1 ; i<argc ; i++)
    {
        if (argv[i][0]=='-') switch(argv[i][1])
        {
            case 'w': f.matchword= true; break;
            case 'b': f.matchbinary= true; break;
            case 'I': f.matchcase= true; break;
            case 'x': f.pattern_is_hex= true; break;
            case 'g': f.pattern_is_guid= true; break;
            case 'v': f.verbose= true; break;
            case 'r': recurse_dirs= true; break;
            case 'l': f.list_only= true; break;
            case 'c': f.count_only= true; break;
            case 'f': f.readcontinuous= true; break;
            case 'M': getarg(argv, i, argc, f.maxfilesize); break;
            case 'X': getarg(argv, i, argc, excludepaths); break;
            case 0:
                      args.push_back("-");
        }
        else if (f.pattern.empty()) 
            f.pattern= argv[i];
        else {
            args.push_back(argv[i]);
        }
    }
    //printf("pattern: %s,   #args=%zd\n", f.pattern.c_str(), args.size());

    std::set<std::string> exclude;
    for (auto i : tokenize(excludepaths, ':'))
        exclude.insert(i);

    if (args.empty())
        args.push_back("-");
    if (!f.compile_pattern())
        return 1;

    for (auto const&arg : args) {
        if (GetFileInfo(arg)==AT_ISDIRECTORY)
            dir_iterator(arg,
                [&](const std::string& fn) { f.searchfile(fn); },
                [recurse_dirs,&exclude](const std::string& fn)->bool { 
                    return exclude.find(fn)==exclude.end() && recurse_dirs;
                }
            );
        else if (arg == "-")
            f.searchstdin();
        else
            f.searchfile(arg);
    }

    return 0;
}

