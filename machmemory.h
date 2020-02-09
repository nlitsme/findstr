#pragma once
#include <mach/vm_map.h>
#include <mach/mach_init.h>
#include <mach/mach_error.h>
#include <mach/mach_host.h>
#include <algorithm>

task_t MachOpenProcessByPid(int pid);
int macosx_get_task_for_pid_rights(void);

class mach_exception : std::exception
{
    kern_return_t err;
    const char* msg;
public:
    mach_exception(kern_return_t err, const char*msg)
        : err(err), msg(msg)
    {
    }
    ~mach_exception()
    {
        mach_error(msg, err);
    }
};


class MachVirtualMemory {
    vm_offset_t _ptr;
    mach_msg_type_number_t _nread;
    uint64_t _startpageofs;
    uint64_t _size;
    uint64_t _enddataofs;

    vm_size_t child_get_pagesize()
    {
      kern_return_t status;
      static vm_size_t g_cached_child_page_size = vm_size_t(-1);

      if (g_cached_child_page_size == vm_size_t(-1))
        {
          status = host_page_size(mach_host_self(), &g_cached_child_page_size);
          /* This is probably being over-careful, since if we
             can't call host_page_size on ourselves, we probably
             aren't going to get much further.  */
          if (status != KERN_SUCCESS) {
            g_cached_child_page_size = 0;
            throw mach_exception(status, "host_page_size");
          }
        }

      return g_cached_child_page_size;
    }


public:
    MachVirtualMemory(task_t tid, uint64_t ofs, uint64_t size)
        : _ptr(0), _nread(0), _startpageofs(0), _size(size), _enddataofs(0)
    {
        vm_size_t pagesize = child_get_pagesize();

        _startpageofs = ofs % pagesize;
        auto baseofs = ofs - _startpageofs;

        auto endofs = ofs + size;

        auto endpageofs = endofs % pagesize;
        auto endpagebase = endofs - endpageofs;
        if (endpageofs) {
            endpageofs -= pagesize;
            endpagebase += pagesize;
        }

        auto kret = vm_read(tid, baseofs, endpagebase-baseofs, &_ptr, &_nread);
        if (kret != KERN_SUCCESS)
            throw mach_exception(kret, "vm_read");

        _enddataofs = std::min(_startpageofs+_size, uint64_t(_nread));
    }
    ~MachVirtualMemory()
    {
        vm_deallocate(mach_task_self(), _ptr, _nread);
    }
    const uint8_t *begin() const
    {
        return (const uint8_t*)_ptr + _startpageofs;
    }
    const uint8_t *end() const
    {
        return (const uint8_t*)_ptr + _enddataofs;
    }
    size_t size() const
    {
        return _enddataofs - _startpageofs;
    }
};


