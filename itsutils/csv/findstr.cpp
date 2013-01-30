#include "util/rw/MmapReader.h"
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include "FileFunctions.h"
#include "stringutils.h"

struct findstr {
    bool matchword;      // modifies pattern
    bool matchbinary;    // modifies pattern, modifies verbose output
    bool matchcase;      // modifies pattern
    bool pattern_is_hex; // modifies verbose output, implies binary
    bool pattern_is_guid;// modifies verbose output, implies binary
    bool verbose;        // modifies ouput
    bool list_only;      // modifies ouput

    std::string pattern;

    findstr() :
        matchword(false),
        matchbinary(false),
        matchcase(false),
        pattern_is_hex(false),
        pattern_is_guid(false),
        verbose(false),
        list_only(false)
    {
    }

    // returns:
    //     NULL   when final match found
    //     last   when only complete matches were found
    //     *      when partial match was found
    template<typename CB>
    const uint8_t *searchrange(const uint8_t *first, const uint8_t *last, CB cb)
    {
        boost::basic_regex<uint8_t> re((const uint8_t*)pattern.c_str(), (const uint8_t*)pattern.c_str()+pattern.size());

        boost::regex_iterator<const uint8_t*> a(first, last, re);
        boost::regex_iterator<const uint8_t*> b;

        const uint8_t *maxpartial= NULL;
        const uint8_t *maxmatch= NULL;

        while (a!=b) {
            auto m= (*a)[0];
            if (m.matched) {
                if (!cb(m.first, m.second))
                    return NULL;
                if (maxmatch==NULL || maxmatch<m.first)
                    maxmatch= m.first;
            }
            else {
                if (maxpartial==NULL || maxpartial<m.first)
                    maxpartial= m.first;
            }
        }
        if (maxmatch>maxpartial)
            return last;

        return maxpartial;
    }

    void searchstdin()
    {
        // see: http://www.boost.org/doc/libs/1_52_0/libs/regex/doc/html/boost_regex/partial_matches.html

        bool nameprinted= false;
        ByteVector buf(65536);
        uint8_t *bufstart= &buf.front();
        uint8_t *bufend= bufstart+buf.size();
        uint64_t offset= 0;

        uint8_t *readptr= bufstart;

        while (true)
        {
            size_t needed= bufend-readptr;
            int n= read(0, readptr, needed);
            if (n==0) {
                usleep(100);
                continue;
            }
            else if (n>needed || n<0)
                break;

            uint8_t *readend= readptr+n;

            const uint8_t *partial= searchrange(bufstart, readend, [bufstart, &offset, &nameprinted, this](const uint8_t *first, const uint8_t *last)->bool {
                if (list_only) {
                    std::cout << "-\n" << std::endl;
                    return false;
                }
                else if (verbose) {
                    if (pattern_is_hex || matchbinary)
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % hexdump(first, last-first);
                    if (pattern_is_guid)
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % guidstring(first);
                    else
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % ascdump(first, last-first);
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
            }
            else {
                readptr= bufstart;
            }

            offset += n;
        }
    }
    static boost::format guidstring(const uint8_t *p)
    {
        return boost::format("%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x")
            % get32le(p+0)
            % get16le(p+4)
            % get16le(p+6)
            % get16be(p+8)
            % p[10] % p[11] % p[12] % p[13] % p[14] % p[15];
    }
    void searchfile(const std::string& fn)
    {
        bool nameprinted= false;
        MmapReader r(fn, MmapReader::readonly);
        searchrange(r.begin(), r.end(), [&fn, &r, &nameprinted, this](const uint8_t *first, const uint8_t *last)->bool {
            if (list_only) {
                std::cout << fn << std::endl;
                return false;
            }
            else if (verbose) {
                if (pattern_is_hex || matchbinary)
                    std::cout << boost::format("%s %08x %s\n") % (first-r.begin()) % fn % hexdump(first, last-first);
                if (pattern_is_guid)                                              
                    std::cout << boost::format("%s %08x %s\n") % (first-r.begin()) % fn % guidstring(first);
                else                                                              
                    std::cout << boost::format("%s %08x %s\n") % (first-r.begin()) % fn % ascdump(first, last-first);
            }
            else {
                if (!nameprinted) {
                    std::cout << fn << std::endl;
                    std::cout << "\t";
                }
                else {
                    std::cout << ", ";
                }
                std::cout << boost::format("%08x") % (first-r.begin());
                nameprinted= true;
            }
            return true;
        });
        if (nameprinted)
            std::cout << std::endl;
    }

};
int main(int argc, char**argv)
{
    bool recurse_dirs= false;
    StringList args;
    findstr  f;

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
        }
        else if (f.pattern.empty()) 
            f.pattern= argv[i];
        else {
            args.push_back(argv[i]);
        }
    }
    for (auto arg : args) {
        if (GetFileInfo(arg)==AT_ISDIRECTORY)
            dir_iterator(arg, [&f](const std::string& fn) { f.searchfile(fn); }, [recurse_dirs](const std::string& fn)->bool { return recurse_dirs; } );
        else if (arg == "-")
            f.searchstdin();
        else
            f.searchfile(arg);
    }

    return 0;
}

