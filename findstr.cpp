// note: boost::regex is much faster than std::regex
#ifdef USE_BOOST_REGEX
#include <boost/regex.hpp>
#define BASIC_REGEX boost::basic_regex
#define REGEX_ITER  boost::regex_iterator
#define REGEX_MATCH boost::regex_match
#define REGEX_CONST boost::regex_constants
#define PARTIALARG  , boost::match_partial
#endif

#ifdef USE_STD_REGEX
#include <regex>
#define BASIC_REGEX std::basic_regex
#define REGEX_ITER  std::regex_iterator
#define REGEX_MATCH std::regex_match
#define REGEX_CONST std::regex_constants
#define PARTIALARG
#endif

#include <boost/algorithm/searching/knuth_morris_pratt.hpp>
#include <boost/algorithm/searching/boyer_moore.hpp>
#include <boost/algorithm/searching/boyer_moore_horspool.hpp>


#include <experimental/functional>

using namespace std::string_literals;


//#include <boost/format.hpp>
//#define FORMATTER boost::format

//#include "FileFunctions.h"
//#include "stringutils.h"
#include "argparse.h"
#include "formatter.h"
#include "stringlibrary.h"
#include "datapacking.h"
#include "fhandle.h"
#include "mmem.h"
#include "fslibrary.h"

#include <set>
#include <fcntl.h>

#define catchall(call, arg) \
    try { \
        call; \
    } \
    catch(const std::exception& e) { \
        print("EXCEPTION in %s - %s\n", arg, e.what()); \
    } \
    catch(...) { \
        print("EXCEPTION in %s\n", arg); \
    }

/*
 *
 *
 */
class hexpattern {
    std::string pattern;

    static int convertnyble(char c)
    {
        if (c < '0')
            return -1;
        if (c <= '9')
            return c - '0';
        if (c=='?')
            return -2;
        if (c < 'A')
            return -1;
        if (c <= 'F')
            return c - 'A' + 10;
        if (c < 'a')
            return -1;
        if (c <= 'f')
            return c - 'a' + 10;
        return -1;
    }

public:
    hexpattern(const char *first, const char *last)
        : pattern(first, last)
    {
    }

    /*
     *  decodes a single hex pattern chunk into a data and mask pair.
     *
     *  A chunk is a sequence of hex and wildcard characters, separated from
     *  other chunks by one or more spaces.
     *
     */
    auto decodechunk(const std::string& chunk)
    {
        std::vector<uint8_t> data;  uint8_t datavalue = 0;
        std::vector<uint8_t> mask;  uint8_t maskvalue = 0;

        bool hi = true;
        for (auto c : chunk)
        {
            int nyble = convertnyble(c);
            if (nyble==-1)
                continue;

            if (nyble==-2) {
                if (hi) {
                    datavalue = 0;
                    maskvalue = 0;
                }
            }
            else {
                int nyble = convertnyble(c);
                if (hi) {
                    datavalue = nyble << 4;
                    maskvalue = 0xF0;
                }
                else {
                    datavalue |= nyble;
                    maskvalue |= 0x0F;
                }
            }

            if (!hi) {
                data.push_back(datavalue);
                mask.push_back(maskvalue);
            }
            hi = !hi;
        }
        return std::make_pair(data, mask);
    }

    /*
     *  decodes the hex pattern into a pair of 'data' and 'mask'
     *  where 'mask' indicates the wildcards.
     *
     */
    auto decode()
    {
        auto validdigit = [](char c){ return c == '?' || isxdigit(c); };
        auto invaliddigit = [&](char c){ return !validdigit(c); };

        // determine pattern word size, and split into chunks.
        std::vector<std::string> chunks;
        std::set<int> sizes;
        auto i= pattern.c_str();
        auto last = pattern.c_str() + pattern.size();
        while (i!=last) {
            auto j= std::find_if(i, last, validdigit);
            if (j==last)
                break;
            i= std::find_if(j, last, invaliddigit);
            chunks.emplace_back(j,i);
            sizes.insert(i-j);
        }

        std::vector<uint8_t> data;
        std::vector<uint8_t> mask;


        // do a byteswap when the entire pattern consists of 16, 32, 64 or 128 bit chunks.
        std::set<int> oksizes = { 4, 8, 16, 32 };

        bool endianconvert = (sizes.size() == 1) && (oksizes.find(*sizes.begin()) != oksizes.end());

        for (auto & chunk : chunks) {
            auto binary = decodechunk(chunk);
            if (endianconvert) {
                data.insert(data.end(), binary.first.rbegin(), binary.first.rend());
                mask.insert(mask.end(), binary.second.rbegin(), binary.second.rend());
            }
            else {
                data.insert(data.end(), binary.first.begin(), binary.first.end());
                mask.insert(mask.end(), binary.second.begin(), binary.second.end());
            }
        }

        return std::make_pair(data, mask);
    }

    /*
     *  converts the hex pattern to a regular expression.
     */
    std::string getregex()
    {
        auto datamask = decode();

        std::string regex;
        for (int i=0 ; i<datamask.first.size() ; i++)
        {
            switch(datamask.second[i])
            {
                case 0: regex += "."; break;
                case 0xF0: regex += stringformat("[\\x%02x-\\x%02x]", datamask.first[i]&0xF0, (datamask.first[i]&0xF0) | 0x0F); break;
                case 0x0F:
                           {
                               regex += "[";
                               for (int c = 0 ; c < 0x100 ; c += 0x10)
                                   regex += stringformat("\\x%02x", c+(datamask.first[i]&0x0F));
                               regex += "]";
                           }
                           break;
                case 0xFF: regex += stringformat("\\x%02x", datamask.first[i]); break;
            }
        }

        return regex;
    }
};

/*
 * The various search algoritms implemented in findstr.
 */
enum SearchType {
    REGEX_SEARCH,
    STD_SEARCH,
    STD_BOYER_MOORE,
    STD_BOYER_MOORE_HORSPOOL,
    BOOST_BOYER_MOORE,
    BOOST_BOYER_MOORE_HORSPOOL,
    BOOST_KNUTH_MORRIS_PRATT,
};
struct findstr {
    bool matchword = false;      // modifies pattern
    bool matchbinary = false;    // modifies pattern, modifies verbose output
    bool matchcase = false;      // modifies pattern
    bool pattern_is_hex = false; // modifies verbose output, implies binary
    bool pattern_is_guid = false;// modifies verbose output, implies binary
    bool verbose = false;        // modifies ouput
    bool list_only = false;      // modifies ouput
    bool count_only = false;     // modifies ouput
    bool readcontinuous = false; // read until ctrl-c, instead of until eof
    bool use_sequential = false; // use read, instead of mmap
    uint64_t maxfilesize = 0;
    bool nameprinted = false;
    int matchcount = 0;
    SearchType searchtype = REGEX_SEARCH;

    std::string pattern;
    std::vector<std::pair<std::vector<uint8_t>,std::vector<uint8_t>>> bytemasks;

    // returns:
    //     NULL   when final match found
    //     last   when only complete matches were found
    //     *      when partial match was found
    template<typename CB>
    const char *regexsearch(const BASIC_REGEX<char>& re, const char *first, const char *last, CB cb)
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

    /*
     *  perform any of the standard library search algorithms.
     */
    template<typename SEARCH, typename CB>
    const char *stringsearch(const char *first, const char *last, CB cb)
    {
        for (auto& hp : bytemasks)
        {
            auto searcher =  SEARCH((const char*)&hp.first.front(), (const char*)&hp.first.front()+hp.first.size());
            auto p = first;
            while (p!=last) {
                auto f = std::search(p, last, searcher);
                if (f == last)
                    break;
                cb((const char*)f, (const char*)f+hp.first.size());
                p = f+1;
            }
        }
        return last;
    }

    /*
     *  perform any of the boost library search algorithms.
     */
    template<typename SEARCH, typename CB>
    const char *boostsearch(const char *first, const char *last, CB cb)
    {
        for (auto& hp : bytemasks)
        {
            //auto searcher =  std::experimental::boyer_moore_searcher(hp.first.begin(), hp.first.end());
            //auto searcher =  std::experimental::boyer_moore_horspool_searcher(hp.first.begin(), hp.first.end());
            auto searcher =  SEARCH((const char*)&hp.first.front(), (const char*)&hp.first.front()+hp.first.size());
            auto p = first;
            while (p!=last) {
                auto f = searcher(p, last);
                if (f.first == last)
                    break;
                cb((const char*)f.first, (const char*)f.first+hp.first.size());
                p = f.first+1;
            }
        }
        return last;
    }

    void searchstdin()
    {
        filehandle f(0);
        searchsequential(f, "-");
    }

    // TODO: extract 'searchers'  into a search wrapper,
    //       one for regex, one for boost, one for std.
    void searchsequential(filehandle& f, const std::string& origin)
    {
        //printf("searching stdin\n");
        // see: http://www.boost.org/doc/libs/1_52_0/libs/regex/doc/html/boost_regex/partial_matches.html

        nameprinted= false;
        matchcount= 0;

        std::vector<char> buf(0x100000);
        char *bufstart= &buf.front();
        char *bufend= bufstart+buf.size();
        uint64_t offset= 0;

        char *readptr= bufstart;

        BASIC_REGEX<char> re(pattern.c_str(), pattern.c_str()+pattern.size(), matchcase ? REGEX_CONST::nosubs : REGEX_CONST::nosubs|REGEX_CONST::icase);

        while (true)
        {
            int needed= bufend-readptr;
            //print("%08x ; %x: needed= %d -> %p .. %p .. %p\n", offset, lseek(f, 0, 1), needed, bufstart, readptr, bufend);
            int n= read(f, readptr, needed);
            if (n==0) {
                if (readcontinuous) {
                    //printf("stdin: waiting for more\n");
                    usleep(100);
                    continue;
                }
                //print("read empty(need=%d), pos=%d\n", needed, lseek(f, 0, 1));
                break;
            }
            else if (n>needed || n<0)
            {
                //perror("read");
                //print("n=%d\n", n);
                break;
            }

            char *readend= readptr+n;
            const char *partial;

            partial = search(re, bufstart, readend, [&origin, bufstart, offset, this](const char *first, const char *last)->bool {
                return writeresult(origin, bufstart, offset, first, last);
            });

            // avoid too large partial matches
            if (partial-bufstart < (int)buf.size()/2)
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
            print("%6d %s\n", matchcount, "-");
        if (nameprinted)
            print("\n");
    }
    static std::string guidstring(const uint8_t *p)
    {
        struct guid {
            uint32_t a;
            uint16_t b;
            uint16_t c;
            uint8_t  d[8];
        };
        const guid *g = (const guid*)p;
        return stringformat("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                g->a, g->b, g->c, g->d[0], g->d[1],
                g->d[2], g->d[3], g->d[4], g->d[5], g->d[6], g->d[7]);
    }
    void searchfile(const std::string& fn)
    {
        filehandle f = open(fn.c_str(), O_RDONLY);
        searchhandle(f, stringformat("file:%s", fn));
    }

    void searchhandle(filehandle& f, const std::string& origin)
    {
        auto size = f.size();
        if (size==0)
            return;
        else if (use_sequential || size < 0)
            searchsequential(f, origin);
        else
            searchmmap(f, size, origin);
    }
    void searchmmap(filehandle& f, uint64_t fsize, const std::string& origin)
    {
        if (maxfilesize && fsize>=maxfilesize) {
            if (verbose)
                print("skipping large file %s\n", origin);
            return;
        }

        mappedmem r(f, 0, fsize, PROT_READ);

        nameprinted= false;
        matchcount= 0;

        BASIC_REGEX<char> re(pattern.c_str(), pattern.c_str()+pattern.size(), matchcase ? REGEX_CONST::nosubs : REGEX_CONST::nosubs|REGEX_CONST::icase);

        auto bufstart = (const char*)r.begin();

        search(re, bufstart, (const char*)r.end(), [&origin, bufstart, this](const char *first, const char *last)->bool {
            return writeresult(origin, bufstart, 0, first, last);
        });


        if (count_only)
            print("%6d %s\n", matchcount, origin);
        if (nameprinted)
            print("\n");

    }
    bool writeresult(const std::string&origin, const char *bufstart, uint64_t offset, const char *first, const char *last)
    {
        matchcount++;
        if (count_only)
            return true;
        if (list_only) {
            print("%s\n", origin);
            return false;
        }
        else if (verbose) {
            if (matchbinary)
                print("%s %08x %-b\n", origin, offset+first-bufstart, Hex::dumper((const uint8_t*)first, last-first));
            else if (pattern_is_guid)                                                 
                print("%s %08x %s\n", origin, offset+first-bufstart, guidstring((const uint8_t*)first));
            else                                                                 
                print("%s %08x %+b\n", origin, offset+first-bufstart, Hex::dumper((const uint8_t*)first, last-first));
        }
        else {
            if (!nameprinted) {
                print("%s\n\t", origin);
            }
            else {
                print(", ");
            }
            print("%08x", offset+first-bufstart);
            nameprinted= true;
        }
        return true;
    }


    template<typename CB>
    const char *search(const BASIC_REGEX<char>& re, const char*bufstart, const char*bufend, CB callback)
    {
        switch(searchtype) {
        case REGEX_SEARCH:
            return regexsearch(re, bufstart, bufend, callback);
        case STD_SEARCH:
            return stringsearch<std::experimental::default_searcher<const char*>, CB>(bufstart, bufend, callback);
            break;
        case STD_BOYER_MOORE:
            return stringsearch<std::experimental::boyer_moore_searcher<const char*>, CB>(bufstart, bufend, callback);
            break;
        case STD_BOYER_MOORE_HORSPOOL:
            return stringsearch<std::experimental::boyer_moore_horspool_searcher<const char*>, CB>(bufstart, bufend, callback);
            break;
        case BOOST_BOYER_MOORE:
            return boostsearch<boost::algorithm::boyer_moore<const char*>, CB>(bufstart, bufend, callback);
            break;
        case BOOST_BOYER_MOORE_HORSPOOL:
            return boostsearch<boost::algorithm::boyer_moore_horspool<const char*>, CB>(bufstart, bufend, callback);
            break;
        case BOOST_KNUTH_MORRIS_PRATT:
            return boostsearch<boost::algorithm::knuth_morris_pratt<const char*>, CB>(bufstart, bufend, callback);
            break;
        }
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
            pattern = pattern + "|" + make_unicode_pattern(pattern);
        }
        return true;
    }

    bool compile_hex_pattern()
    {
        // format:  <pattern> [ "|" <pattern> ... ]
        //
        //     XX XX XX XX
        //     XXXXXXXX     <-- convert to little endian
        //
        std::vector<hexpattern> patternlist;

        auto i = pattern.c_str();
        auto last = pattern.c_str() + pattern.size();
        while (i != last)
        {
            auto j = std::find(i, last, '|');
            patternlist.emplace_back(i, j);
            i = (j==last) ? j : j+1;
        }

        if (searchtype == REGEX_SEARCH) {
            pattern.clear();
            for (auto & hp : patternlist)
            {
                if (!pattern.empty())
                    pattern += "|";
                pattern += hp.getregex();
            }
        }
        else {
            for (auto & hp : patternlist)
                bytemasks.push_back(hp.decode());
        }
        return true;
    }


    bool compile_guid_pattern()
    {
        std::vector<uint8_t> guid(16);
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
                    unchecked::set32le(&guid[i], l1);
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
                    unchecked::set32le(&guid[i], w);
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
    std::string make_unicode_pattern(std::string& apat)
    {
        std::string upat;
        // translate [...] -> [...]\x00
        // translate (...) { * | + | ? | {\d*,\d*} } \??  -> (...) QUANT
        // normal  : not { * | + | ? | . | { | ( | ) | ^ | $ | [ | ] | \ }   -> .\x00     ... }
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

/*
 * object which returns an iterator, iterating over all substrings.
 */
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
void usage()
{
    print("Usage: findstr [options]  pattern  files...\n");
    print("   -w       (regex) match words\n");
    print("   -b       binary match ( no unicode match )\n");
    print("   -I       case sensitive match\n");
    print("   -x       pattern is in hex\n");
    print("   -g       pattern is a guid\n");
    print("   -v       verbose\n");
    print("   -r       recurse\n");
    print("   -l       list matching files\n");
    print("   -c       count number of matches per file\n");
    print("   -f       follow, keep checking file for new data\n");
    print("   -M NUM   max file size\n");
    //print("   -X LIST   exclude paths\n");
    print("   -S       search algorithm: regex, std, stdbm, stdbmh, boostbm, boostbmh, boostkmp\n");
    print("   -Q       use posix::read, instead of posix::mmap\n");
}
int main(int argc, char**argv)
{
    bool recurse_dirs= false;
    std::vector<std::string> args;
    findstr  f;
    std::string excludepaths;

    for (auto& arg : ArgParser(argc, argv))
        switch (arg.option())
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
            case 'M': f.maxfilesize = arg.getint(); break;
            case 'X': excludepaths = arg.getstr(); break;
            case 'S': 
                      {
                      auto mode = arg.getstr();
                      if (mode == "regex"s) f.searchtype = REGEX_SEARCH;
                      else if (mode == "std"s) f.searchtype = STD_SEARCH;
                      else if (mode == "stdbm"s) f.searchtype = STD_BOYER_MOORE;
                      else if (mode == "stdbmh"s) f.searchtype = STD_BOYER_MOORE_HORSPOOL;
                      else if (mode == "boostbm"s) f.searchtype = BOOST_BOYER_MOORE;
                      else if (mode == "boostbmh"s) f.searchtype = BOOST_BOYER_MOORE_HORSPOOL;
                      else if (mode == "boostkmp"s) f.searchtype = BOOST_KNUTH_MORRIS_PRATT;
                      }
                      break;
            case 'Q': f.use_sequential = true; break;
            case 0:
                      args.push_back("-");
                      break;
            case -1:
                if (f.pattern.empty()) 
                    f.pattern= arg.getstr();
                else {
                    args.push_back(arg.getstr());
                }
                break;
            default:
                usage();
                return 1;
        }

    if (f.pattern.empty()) {
        usage();
        return 1;
    }
    if (f.pattern_is_hex) {
        f.matchbinary = true;
        f.matchcase = true;
    }
    std::set<std::string> exclude;
    for (auto i : tokenize(excludepaths, ':'))
        exclude.insert(i);

    if (args.empty())
        args.push_back("-");
    if (!f.compile_pattern())
        return 1;

    for (auto const&arg : args) {
        if (arg == "-")
            f.searchstdin();
        else {
            struct stat st;
            if (-1==stat(arg.c_str(), &st))
                continue;

            if ((st.st_mode&S_IFMT) == S_IFDIR) {
                if (recurse_dirs)
                    for (auto fn : fileenumerator(arg))
                        catchall(f.searchfile(fn), fn);
            }
            //      [recurse_dirs,&exclude](const std::string& fn)->bool { 
            //          return exclude.find(fn)==exclude.end() && recurse_dirs;
            //      }
            else {
                catchall(f.searchfile(arg), arg);
            }
        }
    }

    return 0;
}

