#include "util/rw/MmapReader.h"
#include <boost/regex.hpp>
#include <boost/format.hpp>
#include "FileFunctions.h"
#include "stringutils.h"


// todo: why does  basic_regex<uint8_t>  throw a 'bad_cast' exception?

struct findstr {
    bool matchword;      // modifies pattern
    bool matchbinary;    // modifies pattern, modifies verbose output
    bool matchcase;      // modifies pattern
    bool pattern_is_hex; // modifies verbose output, implies binary
    bool pattern_is_guid;// modifies verbose output, implies binary
    bool verbose;        // modifies ouput
    bool list_only;      // modifies ouput
    bool readcontinuous; // read until ctrl-c, instead of until eof

    std::string pattern;

    findstr() :
        matchword(false),
        matchbinary(false),
        matchcase(false),
        pattern_is_hex(false),
        pattern_is_guid(false),
        verbose(false),
        list_only(false),
        readcontinuous(false)
    {
    }

    // returns:
    //     NULL   when final match found
    //     last   when only complete matches were found
    //     *      when partial match was found
    template<typename CB>
    const char *searchrange(const char *first, const char *last, CB cb)
    {
        boost::basic_regex<char> re(pattern.c_str(), pattern.c_str()+pattern.size());

        boost::regex_iterator<const char*> a(first, last, re, boost::match_partial);
        boost::regex_iterator<const char*> b;

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
        std::vector<char> buf(256);
        char *bufstart= &buf.front();
        char *bufend= bufstart+buf.size();
        uint64_t offset= 0;

        char *readptr= bufstart;

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

            const char *partial= searchrange(bufstart, readend, [bufstart, offset, &nameprinted, this](const char *first, const char *last)->bool {
                if (list_only) {
                    std::cout << "-\n" << std::endl;
                    return false;
                }
                else if (verbose) {
                    if (pattern_is_hex || matchbinary)
                        std::cout << boost::format("- %08x %s\n") % (offset+first-bufstart) % hexdump((const uint8_t*)first, last-first);
                    if (pattern_is_guid)
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
        //printf("searching %s\n", fn.c_str());
        bool nameprinted= false;
        MmapReader r(fn, MmapReader::readonly);
        const char*filefirst= (const char*)r.begin();
        const char*filelast= (const char*)r.end();
        searchrange(filefirst, filelast, [&fn, filefirst, &nameprinted, this](const char *first, const char *last)->bool {
            if (list_only) {
                std::cout << fn << std::endl;
                return false;
            }
            else if (verbose) {
                if (pattern_is_hex || matchbinary)
                    std::cout << boost::format("%s %08x %s\n") % fn % (first-filefirst) % hexdump((const uint8_t*)first, last-first);
                if (pattern_is_guid)                                                   
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
            case 'f': f.readcontinuous= true; break;
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

    if (args.empty())
        args.push_back("-");
    //f.compile_pattern();

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

