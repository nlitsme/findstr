/*
 * A tool for searching patterns, text, or hex in binaries.
 *
 * Author: (C) 2004-2019  Willem Hengeveld <itsme@xs4all.nl>
 */


/*
 * Choose between the boost and std library regex implementation
 * note: boost::regex is much faster than std::regex
 */
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

#ifdef USE_BOOST_REGEX
#include <boost/algorithm/searching/knuth_morris_pratt.hpp>
#include <boost/algorithm/searching/boyer_moore.hpp>
#include <boost/algorithm/searching/boyer_moore_horspool.hpp>
#endif

// NOTE: in gcc this is not experimental, for clang it is.
#ifdef __GLIBCXX__
// https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_macros.html
#include <functional>
#define SEARCHERNS std
#endif

#ifdef _LIBCPP_VERSION
#include <experimental/functional>
#define SEARCHERNS std::experimental
#endif

using namespace std::string_literals;


#include "argparse.h"
#include "formatter.h"
#include "stringlibrary.h"
#include "datapacking.h"
#include "fhandle.h"
#include "mmem.h"
#include "fslibrary.h"

#include <set>
#include <fcntl.h>

#ifdef WITH_MEMSEARCH
// from hexdumper
#include "machmemory.h"
#endif

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

//
// TODO: add option to specify what is printed for matches:
//       - only offset
//       - the offset and the matching data.
//       - the 'record' containing the match,
//           where 'record' can be a CR/LF terminated line,
//           or a 'CSV' record, or a NUL terminated item.
//           or a fixed sized block of data.
//
typedef std::vector<uint8_t> ByteVector;
typedef std::pair<ByteVector,ByteVector> ByteMaskType;

/*
 *  class which defines how hex-patterns are handled:
 *    - parsing
 *    - convert to regex
 *    - convert to bytemask
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
    ByteMaskType decodechunk(const std::string& chunk)
    {
        ByteVector data;  uint8_t datavalue = 0;
        ByteVector mask;  uint8_t maskvalue = 0;

        bool hi = true;
        for (auto c : chunk)
        {
            int nyble = convertnyble(c);
            if (nyble == -1)
                continue;

            if (nyble == -2) {
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
    auto getchunks()
    {
        auto validdigit = [](char c){ return c == '?' || isxdigit(c); };
        auto invaliddigit = [&](char c){ return !validdigit(c); };

        // determine pattern word size, and split into chunks.
        std::vector<std::string> chunks;
        auto i = pattern.c_str();
        auto last = pattern.c_str() + pattern.size();
        while (i != last) {
            auto j = std::find_if(i, last, validdigit);
            if (j == last)
                break;
            i = std::find_if(j, last, invaliddigit);
            chunks.emplace_back(j,i);
        }

        return chunks;
    }

    /*
     *  decodes the hex pattern into a pair of 'data' and 'mask'
     *  where 'mask' indicates the wildcards.
     */
    ByteMaskType getbytemask()
    {
        // do a byteswap when the entire pattern consists of 16, 32, 64 or 128 bit chunks.
        std::set<int> oksizes = { 4, 8, 16, 32 };

        auto chunks = getchunks();
        std::set<int> sizes;
        for (auto& c : chunks)
            sizes.insert(c.size());

        bool endianconvert = (sizes.size() == 1) && (oksizes.find(*sizes.begin()) != oksizes.end());

        ByteVector data;
        ByteVector mask;
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
    ByteMaskType getguidmask()
    {
        auto chunks = getchunks();
        if (chunks.size() != 5)
            throw "not a guid";

        ByteVector data;
        ByteVector mask;

        // wwwwwwww-xxxx-xxxx-bbbb-bbbbbbbbbbbb
        std::vector<bool> endiancv = { true, true, true, false, false };

        for (int i = 0 ; i < 5 ; i++) {
            auto& chunk = chunks[i];
            bool cv = endiancv[i];

            auto binary = decodechunk(chunk);
            if (cv) {
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
        return datamask2regex(getbytemask());
    }
    std::string guidregex()
    {
        return datamask2regex(getguidmask());
    }
    std::string datamask2regex(const ByteMaskType & datamask)
    {
        auto & data = datamask.first;
        auto & mask = datamask.second;

        std::string regex;
        for (unsigned i = 0 ; i < data.size() ; i++)
        {
            switch(mask[i])
            {
                case 0: regex += "."; break;
                case 0xF0: regex += stringformat("[\\x%02x-\\x%02x]", data[i] & 0xF0, (data[i] & 0xF0) | 0x0F); break;
                case 0x0F:
                           {
                               regex += "[";
                               for (int c = 0 ; c < 0x100 ; c += 0x10)
                                   regex += stringformat("\\x%02x", c + (data[i] & 0x0F));
                               regex += "]";
                           }
                           break;
                case 0xFF: regex += stringformat("\\x%02x", data[i]); break;
            }
        }

        return regex;
    }
};


/*
 *   the various search implementations
 */
typedef std::function<bool(const char*, const char*)>  CallbackType;
class SearchBase {
public:
    virtual ~SearchBase() { }
    virtual const char *search(const char *first, const char *last, CallbackType cb) = 0;
};
class regexsearcher : public SearchBase {
    const BASIC_REGEX<char> re;
public:
    regexsearcher(const std::string& pattern, bool matchcase)
        : re(pattern.c_str(), pattern.c_str() + pattern.size(),
                BASIC_REGEX<char>::flag_type(REGEX_CONST::nosubs | (matchcase ? 0 :  REGEX_CONST::icase)))

    {

    }

    // returns:
    //     NULL   when final match found
    //     last   when only complete matches were found
    //     *      when partial match was found
    const char *search(const char *first, const char *last, CallbackType cb)
    {
        REGEX_ITER<const char*> a(first, last, re   PARTIALARG);
        REGEX_ITER<const char*> b;

        const char *maxpartial = NULL;
        const char *maxmatch = NULL;

        //printf("searchrange(%p, %p)\n", first, last);
        while (a != b) {
            auto m = (*a)[0];
            //printf("    match %d  %p..%p\n", m.matched, m.first, m.second);
            if (m.matched) {
                if (!cb(m.first, m.second)) {
                    //printf("searchrange: stopping\n");
                    return NULL;
                }
                if (maxmatch == NULL || maxmatch < m.first)
                    maxmatch = m.first;
            }
            else {
                if (maxpartial == NULL || maxpartial < m.first)
                    maxpartial = m.first;
            }

            ++a;
        }
        if ((maxmatch == NULL && maxpartial == NULL) || maxmatch > maxpartial) {
            //printf("searchrange: no partial match\n");
            return last;
        }

        //printf("searchrange: partial match @%lx\n", maxpartial-first);
        return maxpartial;
    }
};


/*
 *  plain stringsearch, ignoring wildcards.
 */
template<typename SEARCH>
class stringsearch : public SearchBase {
    std::vector<std::tuple<size_t, SEARCH>> patterns;
public:
    static bool is_full_mask(const ByteVector& mask)
    {
        return std::find_if(mask.begin(), mask.end(), [](auto b) { return b != 0xFF; }) == mask.end();
    }
    stringsearch(const std::vector<ByteMaskType> & bytemasks)
    {
        bool mask_warning = false;
        for (auto& hp : bytemasks) {
            auto & data = hp.first;

            if (!is_full_mask(hp.second) && !mask_warning) {
                print("WARNING: ignoring bytemask\n");
                mask_warning = true;
            }

            patterns.emplace_back(data.size(), SEARCH{(const char*)&data.front(), (const char*)&data.front() + data.size()});
        }
    }

    /*
     *  perform any of the boost library search algorithms.
     */
    const char *search(const char *first, const char *last, CallbackType cb)
    {
        for (auto& hp : patterns)
        {
            auto size = std::get<0>(hp);
            auto & searcher = std::get<1>(hp);

            auto p = first;
            while (p != last) {
                auto f = std::search(p, last, searcher);
                if (f == last)
                    break;
                if (!cb((const char*)f, (const char*)f + size))
                    return NULL;
                p = f + 1;
            }
        }
        return last;
    }
};

/*
 * byte mask search
 */
class masksearch : public SearchBase {
    std::vector<ByteMaskType> patterns;
public:
    masksearch(const std::vector<ByteMaskType> & bytemasks)
        : patterns(bytemasks)
    {
        for (auto & bm : patterns)
            if (bm.first.size() != bm.second.size())
                print("WARNING: size mismatch between pattern and bytemask\n");

        // todo: maybe i can optimize this by splitting the patterns in 'full' and 'partial' sequences.
        //    where 'full' is a sequence of bytes which has mask == 0xff
    }
    const char *maskedsearch(const char *first, const char *last, ByteMaskType& bm)
    {
        auto p = first;

        // bytes
        auto b = &bm.first[0];
        auto bend = b + bm.first.size();

        // mask 
        auto m = &bm.second[0];

        while (p != last)
        {
            if (((*p ^ *b)&(*m)) == 0) {
                ++b; ++m;
                if (b==bend)
                    return p - bm.first.size();
            }
            else {
                b = &bm.first[0];
            }
        }
        return last;
    }

    /*
     *  do a bytemask search.
     */
    const char *search(const char *first, const char *last, CallbackType cb)
    {
        for (auto& bm : patterns)
        {
            auto size = bm.first.size();

            auto p = first;
            while (p != last) {
                auto f = maskedsearch(p, last, bm);
                if (f == last)
                    break;
                if (!cb((const char*)f, (const char*)f + size))
                    return NULL;
                p = f + 1;
            }
        }
        return last;
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
    BYTEMASK_SEARCH,
};


struct findstr {
    bool matchword = false;      // modifies pattern
    bool matchbinary = false;    // modifies pattern, modifies verbose output
    bool matchcase = false;      // modifies pattern
    bool matchstart = false;      // modifies pattern
    bool pattern_is_hex = false; // modifies verbose output, implies binary
    bool pattern_is_guid = false;// modifies verbose output, implies binary
    int verbose = 0;             // modifies ouput
    bool list_only = false;      // modifies ouput
    bool count_only = false;     // modifies ouput
    bool readcontinuous = false; // read until ctrl-c, instead of until eof
    bool use_sequential = false; // use read, instead of mmap
    uint64_t maxfilesize = 0;
    bool nameprinted = false;
    int matchcount = 0;

#ifdef WITH_MEMSEARCH
    int pid = 0;
    uint64_t memoffset = 0;
    uint64_t memsize = 0;
#endif

    SearchType searchtype = REGEX_SEARCH;

    std::string pattern;
    std::vector<ByteMaskType> bytemasks;

#ifdef WITH_MEMSEARCH
    void searchmemory()
    {
        task_t task = MachOpenProcessByPid(pid);
        auto searcher = makesearcher();

        MachVirtualMemory mem(task, memoffset, memsize);

        searcher->search((const char*)mem.begin(), (const char*)mem.end(), [&mem, this](const char *first, const char *last)->bool {
            return writeresult("memory", (const char*)mem.begin(), memoffset, first, last);
        });
    }
#endif

    void searchstdin()
    {
        filehandle f(0);
        searchsequential(f, "-");
    }

    void searchsequential(filehandle& f, const std::string& origin)
    {
        //printf("searching stdin\n");
        // see: http://www.boost.org/doc/libs/1_52_0/libs/regex/doc/html/boost_regex/partial_matches.html

        nameprinted = false;
        matchcount = 0;

        std::vector<char> buf(0x100000);
        char *bufstart = &buf.front();
        char *bufend = bufstart + buf.size();
        uint64_t offset = 0;

        auto searcher = makesearcher();

        char *readptr = bufstart;

        while (true)
        {
            int needed = bufend - readptr;
            //print("%08x ; %x: needed = %d -> %p .. %p .. %p\n", offset, lseek(f, 0, 1), needed, bufstart, readptr, bufend);
            int n = read(f, readptr, needed);
            if (n == 0) {
                if (readcontinuous) {
                    //printf("stdin: waiting for more\n");
                    usleep(100);
                    continue;
                }
                //print("read empty(need=%d), pos=%d\n", needed, lseek(f, 0, 1));
                break;
            }
            else if (n > needed || n < 0)
            {
                //perror("read");
                //print("n=%d\n", n);
                break;
            }

            char *readend = readptr + n;
            const char *partial;

            partial = searcher->search(bufstart, readend, [&origin, bufstart, offset, this](const char *first, const char *last)->bool {
                return writeresult(origin, bufstart, offset, first, last);
            });
            if (partial==NULL)  // writeresult told searcher to stop
                break;
            if (matchstart)
                break;

            // avoid too large partial matches
            if (partial - bufstart < (int)buf.size()/2)
                partial = bufstart + buf.size()/2;

            // relocate data for partial matches
            if (partial < readend) {
                memcpy(bufstart, partial, readend - partial);
                readptr = bufstart + (readend - partial);
                n -= (readend - partial);
            }
            else {
                readptr = bufstart;
            }

            offset += n;
        }
        if (count_only)
            print("%6d %s\n", matchcount, "-");
        if (nameprinted)
            print("\n");
    }
    void searchfile(const std::string& fn)
    {
        filehandle f = open(fn.c_str(), O_RDONLY);
        searchhandle(f, fn);
    }

    void searchhandle(filehandle& f, const std::string& origin)
    {
        auto size = f.size();
        if (size == 0)
            return;
        else if (use_sequential || size < 0)
            searchsequential(f, origin);
        else
            searchmmap(f, size, origin);
    }
    void searchmmap(filehandle& f, uint64_t fsize, const std::string& origin)
    {
        if (maxfilesize && fsize >= maxfilesize) {
            if (verbose)
                print("skipping large file %s\n", origin);
            return;
        }

        mappedmem r(f, 0, fsize, PROT_READ);

        nameprinted = false;
        matchcount = 0;

        auto searcher = makesearcher();

        auto bufstart = (const char*)r.begin();

        searcher->search(bufstart, (const char*)r.end(), [&origin, bufstart, this](const char *first, const char *last)->bool {
            return writeresult(origin, bufstart, 0, first, last);
        });


        if (count_only)
            print("%6d %s\n", matchcount, origin);
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

    bool writeresult(const std::string& origin, const char *bufstart, uint64_t offset, const char *first, const char *last)
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
                print("%s %08x %-b\n", origin, offset + first - bufstart, Hex::dumper((const uint8_t*)first, last - first));
            else if (pattern_is_guid)                                                 
                print("%s %08x %s\n", origin, offset + first - bufstart, guidstring((const uint8_t*)first));
            else                                                                 
                print("%s %08x %+b\n", origin, offset + first - bufstart, Hex::dumper((const uint8_t*)first, last - first));
        }
        else {
            if (!nameprinted) {
                print("%s\n\t", origin);
            }
            else {
                print(", ");
            }
            print("%08x", offset + first - bufstart);
            nameprinted = true;
        }
        if (matchstart) {
            return false;
        }
        return true;
    }

    bool compile_pattern()
    {
        // TODO:
        //   - if pattern_is_guid
        //      ... guid_translator  -> replaces XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXX...  with \\xXX...\\xXX
        //   - if pattern_is_hex
        //      ... hex_translator   
        //            -- replaces XX with \\xXX,  
        //                        X.  with [\\xX0-\\xXF]
        //                        .X  with [\\x0X..\\xFX]
        //                        ..  with .
        //            -- does bytes swap on XXXX, XXXXXXXX, etc.
        //   - if 'is simple expr'  :   string [ '|' string ]*
        //                             ;  string = [ char | . ]
        //            -> decode to bytemask
        //
        //   - otherwise  'regex'
        //
        //
        // if 'need unicode' -> append unicode patterns.
        //

        if (pattern_is_hex) {
            return compile_hex_pattern();
        }
        else if (pattern_is_guid) {
            return compile_guid_pattern();
        }
        else {
            if (searchtype != REGEX_SEARCH)
                calculatebytemask();
            if (!matchbinary) {
                pattern = pattern + "|" + make_unicode_pattern(pattern, 2) + "|" + make_unicode_pattern(pattern, 4);

                int n = bytemasks.size();
                for (int i = 0 ; i < n ; i++) {
                    bytemasks.emplace_back(make_unicode_bytemask(bytemasks[i], 2));
                    bytemasks.emplace_back(make_unicode_bytemask(bytemasks[i], 4));
                }
            }
        }
        return true;
    }
    ByteMaskType make_unicode_bytemask(const ByteMaskType& bm, int size)
    {
        ByteVector data;
        ByteVector mask;
        for (unsigned i = 0 ; i < bm.first.size() ; i++)
        {
            data.push_back(bm.first[i]);
            data.push_back(0);
            if (size == 4) {
                data.push_back(0);
                data.push_back(0);
            }
            mask.push_back(bm.second[i]);
            mask.push_back(0);
            if (size == 4) {
                mask.push_back(0);
                mask.push_back(0);
            }
        }
        return std::make_pair(data, mask);
    }

    void calculatebytemask()
    {
        std::vector<std::string> patternlist;

        auto i = pattern.c_str();
        auto last = pattern.c_str() + pattern.size();
        while (i != last)
        {
            auto j = std::find(i, last, '|');
            patternlist.emplace_back(i, j);
            i = (j == last) ? j : j + 1;
        }

        for (auto & txt : patternlist) {
            ByteVector data = converttext(txt);
            ByteVector mask(data.size(), 0xff);
            bytemasks.emplace_back(data, mask);
        }
    }
    ByteVector converttext(const std::string& txt)
    {
        return ByteVector((const uint8_t*)&txt.front(), (const uint8_t*)&txt.front() + txt.size());
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
            i = (j == last) ? j : j + 1;
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
                bytemasks.push_back(hp.getbytemask());
        }
        return true;
    }
    std::shared_ptr<SearchBase> makesearcher()
    {
        switch(searchtype) {
        case REGEX_SEARCH:
            return std::make_shared<regexsearcher>(pattern, matchcase);
        case STD_SEARCH:
            return std::make_shared<stringsearch<std::default_searcher<const char*>>>(bytemasks);
        case STD_BOYER_MOORE:
            return std::make_shared<stringsearch<SEARCHERNS::boyer_moore_searcher<const char*>>>(bytemasks);
        case STD_BOYER_MOORE_HORSPOOL:
            return std::make_shared<stringsearch<SEARCHERNS::boyer_moore_horspool_searcher<const char*>>>(bytemasks);
#ifdef USE_BOOST_REGEX
        case BOOST_BOYER_MOORE:
            return std::make_shared<stringsearch<boost::algorithm::boyer_moore<const char*>>>(bytemasks);
        case BOOST_BOYER_MOORE_HORSPOOL:
            return std::make_shared<stringsearch<boost::algorithm::boyer_moore_horspool<const char*>>>(bytemasks);
        case BOOST_KNUTH_MORRIS_PRATT:
            return std::make_shared<stringsearch<boost::algorithm::knuth_morris_pratt<const char*>>>(bytemasks);
#endif
        case BYTEMASK_SEARCH:
            return std::make_shared<masksearch>(bytemasks);
        }
        throw std::runtime_error("unknown searchtype");
    }

    bool compile_guid_pattern()
    {
        // format:  <guidpattern> [ "|" <guidpattern> ... ]
        std::vector<hexpattern> patternlist;

        auto i = pattern.c_str();
        auto last = pattern.c_str() + pattern.size();
        while (i != last)
        {
            auto j = std::find(i, last, '|');
            patternlist.emplace_back(i, j);
            i = (j == last) ? j : j + 1;
        }

        if (searchtype == REGEX_SEARCH) {
            pattern.clear();
            for (auto & hp : patternlist)
            {
                if (!pattern.empty())
                    pattern += "|";
                pattern += hp.guidregex();
            }
        }
        else {
            for (auto & hp : patternlist)
                bytemasks.push_back(hp.getguidmask());
        }
        return true;
    }
    std::string make_unicode_pattern(std::string& apat, int size)
    {
        std::string upat;
        // translate [...] -> [...]\x00
        // translate (...) { * | + | ? | {\d*,\d*} } \??  -> (...) QUANT
        // normal  : not { * | + | ? | . | { | ( | ) | ^ | $ | [ | ] | \ }   -> .\x00     ... }
        // \xXX    -> \xXX\x00
        // (?[#:=!>]....)

        std::string esc;            // \\x
        std::string charset;        // [a-z]
        std::string quantifier;     // ...{n}

        for (auto c : apat)
        {
            if (!esc.empty()) {
                esc += c;
                if (esc.size() > 1) {
                    if (esc[1] != 'x' || esc.size() == 4) {
                        upat += esc;
                        upat += size == 2 ? "\\x00" : "\\x00\\x00\\x00";
                        esc.clear();
                    }
                }
            }
            else if (c == '\\') {
                esc += c;
            }
            else if (!quantifier.empty()) {
                quantifier += c;
                if (c == '}') {
                    upat += quantifier;
                    quantifier.clear();
                }
            }
            else if (!charset.empty()) {
                charset += c;
                if (c == ']') {
                    upat += charset;
                    upat += size == 2 ? "\\x00" : "\\x00\\x00\\x00";
                    charset.clear();
                }
            }
            else if (c == '[') {
                charset += c;
            }
            else if (c =='{') {
                quantifier += c;
            }
            else if (c != '(' && c != ')' && c != '*' && c != '|' && c != '+' && c != '?' && c != '^' && c != '$') {
                upat += c;
                upat += size == 2 ? "\\x00" : "\\x00\\x00\\x00";
            }
            else {
                // special regex token.
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
            auto isep = std::find(i, last, sep);
            return std::string(i, isep);
        }
        token& operator++()
        {
            ++i;
            i = std::find(i, last, sep);

            return *this;
        }
        token operator++(int)
        {
            token copy = *this;
            operator++();
            return copy;
        }
        bool operator!=(const token& rhs) const
        {
            return i != rhs.i;
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
    print("   -0       only match to start of file\n");
    print("   -l       list matching files\n");
    print("   -c       count number of matches per file\n");
    print("   -f       follow, keep checking file for new data\n");
    print("   -M NUM   max file size\n");
    //print("   -X LIST   exclude paths\n");
    print("   -S NAME  search algorithm: regex, std, stdbm, stdbmh, boostbm, boostbmh, boostkmp\n");
    print("   -Q       use posix::read, instead of posix::mmap\n");
#ifdef WITH_MEMSEARCH
    print("   -o OFS   memory offset to start searching\n");
    print("   -L SIZE  size of memory block to search through\n");
    print("   -h PID   which process to search\n");
#endif
}
int main(int argc, char** argv)
{
    bool recurse_dirs = false;
    std::vector<std::string> args;
    findstr  f;
    std::string excludepaths;

    for (auto& arg : ArgParser(argc, argv))
        switch (arg.option())
        {
            case 'w': f.matchword = true; break;
            case 'b': f.matchbinary = true; break;
            case 'I': f.matchcase = true; break;
            case '0': f.matchstart = true; break;
            case 'x': f.pattern_is_hex = true; break;
            case 'g': f.pattern_is_guid = true; break;
            case 'v': f.verbose += arg.count(); break;
            case 'r': recurse_dirs = true; break;
            case 'l': f.list_only = true; break;
            case 'c': f.count_only = true; break;
            case 'f': f.readcontinuous = true; break;
            case 'M': f.maxfilesize = arg.getint(); break;
            //case 'X': excludepaths = arg.getstr(); break;
#ifdef WITH_MEMSEARCH
            case 'o': f.memoffset = arg.getint(); break;
            case 'L': f.memsize = arg.getint(); break;
            case 'h': f.pid = arg.getint(); break;
#endif
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
                      else if (mode == "mask"s) f.searchtype = BYTEMASK_SEARCH;
                      }
                      break;
            case 'Q': f.use_sequential = true; break;
            case 0:
                      args.push_back("-");
                      break;
            case -1:
                if (f.pattern.empty()) 
                    f.pattern = arg.getstr();
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
    //std::set<std::string> exclude;
    //for (auto i : tokenize(excludepaths, ':'))
    //    exclude.insert(i);

#ifdef WITH_MEMSEARCH
    if (!f.memoffset)
#endif
    if (args.empty())
        args.push_back("-");
    if (!f.compile_pattern())
        return 1;
    if (f.verbose > 1) {
        print("Compiled regex: %s\n", f.pattern);
        for (auto & bm : f.bytemasks) {
            print("Compiled bytes: %-b\n", bm.first);
            print("Compiled  mask: %-b\n", bm.second);
        }
    }
#ifdef WITH_MEMSEARCH
    if (f.memoffset)
        catchall(f.searchmemory(), "memory");
#endif

    for (auto const& arg : args) {
        if (arg == "-")
            f.searchstdin();
        else {
            struct stat st;
            if (-1 == stat(arg.c_str(), &st))
                continue;

            if (S_ISDIR(st.st_mode)) {
                if (recurse_dirs)
                    for (auto [fn, ent] : fileenumerator(arg))
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

